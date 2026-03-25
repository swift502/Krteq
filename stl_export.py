import bpy
import os
import math

def export_specialized_collections(coll_name, settings, export_dir):
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

    if settings.get("clear_rotation", True):
        rot_deg = settings.get("rotation", (0.0, 0.0, 0.0))
        rot_rad = (
            math.radians(rot_deg[0]),
            math.radians(rot_deg[1]),
            math.radians(rot_deg[2])
        )
        merged_obj.rotation_euler = rot_rad

    bpy.context.view_layer.update()

    export_path = os.path.join(export_dir, f"{coll_name}.stl")
    
    try:
        bpy.ops.export_mesh.stl(filepath=export_path, use_selection=True)
        print(f"Success: Exported {coll_name}.stl")
    except AttributeError:
        # Fallback for Blender 4.1+
        bpy.ops.wm.stl_export(filepath=export_path, export_selected_objects=True)
        print(f"Success: Exported {coll_name}.stl (Blender 4.x+)")

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

        target_collections = {
            "Top": {"rotation": (180.0, 0.0, 0.0), "clear_rotation": True},
            "Bottom": {"rotation": (0.0, 0.0, 0.0), "clear_rotation": False},
            "LED": {"rotation": (0.0, 0.0, 0.0), "clear_rotation": True},
        }

        if bpy.context.active_object and bpy.context.active_object.mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')
            
        original_selection = bpy.context.selected_objects
        original_active = bpy.context.active_object

        for coll_name, settings in target_collections.items():
            export_specialized_collections(coll_name, settings, export_dir)

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

# --- 3. YOUR UI PANEL ---
class VIEW3D_PT_custom_export_panel(bpy.types.Panel):
    bl_label = "Production Exporter"
    bl_idname = "VIEW3D_PT_custom_export_panel"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Export Tool'

    def draw(self, context):
        layout = self.layout
        layout.operator("object.custom_export", text="Export Collections to STL", icon='EXPORT')

# --- 4. REGISTRATION ---
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