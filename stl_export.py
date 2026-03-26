import bpy
import os
import math

def export_stl(coll_name, export_dir, rotation, clear_rotation):
    coll = bpy.data.collections.get(coll_name)
    if not coll or not coll.objects:
        return f"Skipping: Collection '{coll_name}' is missing or empty."

    root_obj_original = next((obj for obj in coll.objects if obj.name.startswith("Root")), None)
    
    if not root_obj_original:
        return f"Skipping: No object starting with 'Root' found in '{coll_name}'."

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

    def process_collection(self, coll_name, export_dir, rotation=(0.0, 0.0, 0.0), clear_rotation=True):
        err = export_stl(coll_name, export_dir, rotation, clear_rotation)
        if err:
            self.report({'ERROR'}, err)

    def execute(self, context):
        blend_dir = os.path.dirname(bpy.data.filepath)
        export_dir = os.path.abspath(os.path.join(blend_dir, "..", "production", "stl"))
        
        os.makedirs(export_dir, exist_ok=True)

        if bpy.context.active_object and bpy.context.active_object.mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')
            
        original_selection = bpy.context.selected_objects
        original_active = bpy.context.active_object

        self.process_collection("Top", export_dir, rotation=(180.0, 0.0, 0.0))
        self.process_collection("Top Keychron", export_dir, rotation=(180.0, 0.0, 0.0))
        self.process_collection("Bottom", export_dir, clear_rotation=False)
        self.process_collection("Bottom Keychron", export_dir, clear_rotation=False)
        self.process_collection("LED", export_dir)

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
                
        self.report({'INFO'}, f"STL export complete")
        return {'FINISHED'}

class VIEW3D_PT_custom_export_panel(bpy.types.Panel):
    bl_label = "Krteq"
    bl_idname = "VIEW3D_PT_custom_export_panel"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Krteq'

    def draw(self, context):
        layout = self.layout
        layout.prop(context.scene, "use_keychron_stab")
        layout.operator("object.custom_export", text="STL Export", icon='FILE_3D')

classes = (
    OBJECT_OT_custom_export,
    VIEW3D_PT_custom_export_panel,
)

def register():
    bpy.types.Scene.use_keychron_stab = bpy.props.BoolProperty(
        name="Keychron stab",
        description="Enable Keychron stabilizer",
        default=False
    )
    for cls in classes:
        try:
            bpy.utils.unregister_class(cls)
        except Exception:
            pass
        bpy.utils.register_class(cls)

def unregister():
    if hasattr(bpy.types.Scene, "use_keychron_stab"):
        del bpy.types.Scene.use_keychron_stab

    for cls in reversed(classes):
        try:
            bpy.utils.unregister_class(cls)
        except Exception:
            pass

if __name__ == "__main__":
    register()