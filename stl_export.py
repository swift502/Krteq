import bpy
import os
import math

def export_stl(coll_name, export_dir, rotation=(0.0, 0.0, 0.0), clear_rotation=True):
    coll = bpy.data.collections.get(coll_name)
    if not coll or not coll.objects:
        print(f"Skipping: Collection '{coll_name}' is missing or empty.")
        return

    root_obj_original = next((obj for obj in coll.objects if obj.name.startswith("Root")), None)
    
    if not root_obj_original:
        print(f"Skipping: No object starting with 'Root' found in '{coll_name}'.")
        return

    bpy.ops.object.select_all(action='DESELECT')

    for obj in coll.objects:
        obj.hide_set(False) 
        obj.select_set(True)
    
    bpy.context.view_layer.objects.active = root_obj_original

    bpy.ops.object.duplicate()
    merged_obj = bpy.context.active_object
    
    bpy.ops.object.convert(target='MESH')
    bpy.ops.object.join()

    if clear_rotation:
        rot_rad = (
            math.radians(rotation[0]),
            math.radians(rotation[1]),
            math.radians(rotation[2])
        )
        merged_obj.rotation_euler = rot_rad

    bpy.context.view_layer.update()

    export_path = os.path.join(export_dir, f"{coll_name}.stl")
    
    bpy.ops.wm.stl_export(filepath=export_path, export_selected_objects=True)

    bpy.ops.object.delete()


class OBJECT_OT_custom_export(bpy.types.Operator):
    bl_idname = "object.custom_export"
    bl_label = "Export Specific Collections"
    bl_description = "Merges and exports Top, Bottom, and LED collections"

    def execute(self, context):
        if not bpy.data.filepath:
            self.report({'ERROR'}, "Please save your .blend file first to establish a path!")
            return {'CANCELLED'}

        blend_dir = os.path.dirname(bpy.data.filepath)
        export_dir = os.path.abspath(os.path.join(blend_dir, "..", "production", "stl"))
        
        os.makedirs(export_dir, exist_ok=True)

        if bpy.context.active_object and bpy.context.active_object.mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')
            
        original_selection = bpy.context.selected_objects
        original_active = bpy.context.active_object

        export_stl("Top", export_dir, rotation=(180.0, 0.0, 0.0))
        export_stl("Bottom", export_dir, clear_rotation=False)
        export_stl("LED", export_dir)

        for obj in original_selection:
            try:
                obj.select_set(True)
            except ReferenceError:
                pass 
        if original_active:
            try:
                bpy.context.view_layer.objects.active = original_active
            except ReferenceError:
                pass
                
        print(f"Batch export complete! Check {export_dir}")
        self.report({'INFO'}, "Batch export complete!")
        return {'FINISHED'}

class VIEW3D_PT_custom_export_panel(bpy.types.Panel):
    bl_label = "Production Exporter"
    bl_idname = "VIEW3D_PT_custom_export_panel"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Krteq'

    def draw(self, context):
        layout = self.layout
        layout.operator("object.custom_export", text="Export Collections to STL", icon='EXPORT')

classes = (
    OBJECT_OT_custom_export,
    VIEW3D_PT_custom_export_panel,
)

def register():
    for cls in classes:
        try:
            bpy.utils.unregister_class(cls)
        except Exception:
            pass
        bpy.utils.register_class(cls)

def unregister():
    for cls in reversed(classes):
        try:
            bpy.utils.unregister_class(cls)
        except Exception:
            pass

if __name__ == "__main__":
    register()