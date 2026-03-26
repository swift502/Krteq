import bpy
import os
import math
import bmesh
from mathutils import Euler

def export_stl(coll_name, export_dir, rotation, clear_rotation):
    coll = bpy.data.collections.get(coll_name)
    if not coll or not coll.objects:
        return f"Skipping: Collection '{coll_name}' is missing or empty."

    root_obj_original = next((obj for obj in coll.objects if obj.name.startswith("Root")), None)
    
    if not root_obj_original:
        return f"Skipping: No object starting with 'Root' found in '{coll_name}'."

    depsgraph = bpy.context.evaluated_depsgraph_get()
    merged_bm = bmesh.new()
    source_count = 0
    root_world = root_obj_original.matrix_world.copy()
    root_world_inv = root_world.inverted()

    for obj in coll.objects:
        obj_eval = obj.evaluated_get(depsgraph)
        temp_mesh = bpy.data.meshes.new_from_object(obj_eval, depsgraph=depsgraph)
        if temp_mesh is None:
            continue

        temp_mesh.transform(root_world_inv @ obj.matrix_world)
        merged_bm.from_mesh(temp_mesh)
        bpy.data.meshes.remove(temp_mesh)
        source_count += 1

    if source_count == 0:
        merged_bm.free()
        return f"Skipping: Collection '{coll_name}' has no exportable geometry."

    merged_mesh = bpy.data.meshes.new(f"{coll_name}_export_mesh")
    merged_bm.to_mesh(merged_mesh)
    merged_bm.free()

    merged_obj = bpy.data.objects.new(f"{coll_name}_export_obj", merged_mesh)
    bpy.context.scene.collection.objects.link(merged_obj)

    root_loc, root_rot, root_scale = root_world.decompose()
    add_rot = Euler((
        math.radians(rotation[0]),
        math.radians(rotation[1]),
        math.radians(rotation[2])
    ), 'XYZ').to_quaternion()

    base_rot = Euler((0.0, 0.0, 0.0), 'XYZ').to_quaternion() if clear_rotation else root_rot
    final_rot = base_rot @ add_rot

    merged_obj.location = root_loc
    merged_obj.scale = root_scale
    merged_obj.rotation_mode = 'QUATERNION'
    merged_obj.rotation_quaternion = final_rot

    original_selection = list(bpy.context.selected_objects)
    original_active = bpy.context.view_layer.objects.active

    bpy.ops.object.select_all(action='DESELECT')
    merged_obj.select_set(True)
    bpy.context.view_layer.objects.active = merged_obj

    export_path = os.path.join(export_dir, f"{coll_name}.stl")
    bpy.ops.wm.stl_export(filepath=export_path, export_selected_objects=True)

    bpy.ops.object.select_all(action='DESELECT')
    for obj in original_selection:
        if obj.name in bpy.data.objects:
            obj.select_set(True)
    if original_active and original_active.name in bpy.data.objects:
        bpy.context.view_layer.objects.active = original_active

    bpy.data.objects.remove(merged_obj, do_unlink=True)
    bpy.data.meshes.remove(merged_mesh)

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
        self.process_collection("Bottom", export_dir, clear_rotation=False)
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