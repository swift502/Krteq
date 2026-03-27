import bpy
import os
import math
import bmesh
from mathutils import Euler

def export_stl(op, coll_name, rotation=(0.0, 0.0, 0.0), clear_rotation=True, suffix=None):
    coll = bpy.data.collections.get(coll_name)

    if not coll:
        op.report({'ERROR'}, f"Skipping: Collection '{coll_name}' not found")
        return
    
    if not coll.objects:
        op.report({'ERROR'}, f"Skipping: Collection '{coll_name}' is empty")
        return

    if bpy.context.view_layer.layer_collection.children[coll_name].exclude:
        op.report({'ERROR'}, f"Skipping: Collection '{coll_name}' is excluded from view layer")
        return

    root_obj = next((obj for obj in coll.objects if obj.name.startswith("Root")), None)
    if not root_obj:
        op.report({'ERROR'}, f"Skipping: Root object not found in '{coll_name}'")
        return

    depsgraph = bpy.context.evaluated_depsgraph_get()
    merged_bm = bmesh.new()
    root_world = root_obj.matrix_world.copy()
    root_world_inv = root_world.inverted()

    for obj in coll.objects:
        try:
            temp_mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(depsgraph), depsgraph=depsgraph)
        except Exception as e:
            op.report({'ERROR'}, f"Skipping object '{obj.name}' in '{coll_name}': {e}")
            continue
        temp_mesh.transform(root_world_inv @ obj.matrix_world)
        merged_bm.from_mesh(temp_mesh)
        bpy.data.meshes.remove(temp_mesh)

    merged_mesh = bpy.data.meshes.new(f"{coll_name}_export_mesh")
    merged_bm.to_mesh(merged_mesh)
    merged_bm.free()

    merged_obj = bpy.data.objects.new(f"{coll_name}_export_obj", merged_mesh)
    bpy.context.scene.collection.objects.link(merged_obj)

    root_loc, root_rot, root_scale = root_world.decompose()
    add_rot = Euler([math.radians(r) for r in rotation], 'XYZ').to_quaternion()
    base_rot = Euler((0, 0, 0), 'XYZ').to_quaternion() if clear_rotation else root_rot

    merged_obj.location = root_loc
    merged_obj.scale = root_scale
    merged_obj.rotation_mode = 'QUATERNION'
    merged_obj.rotation_quaternion = base_rot @ add_rot

    for scene_obj in bpy.context.view_layer.objects:
        scene_obj.select_set(False)
    merged_obj.select_set(True)

    export_dir = os.path.abspath(os.path.join(os.path.dirname(bpy.data.filepath), "..", "production", "stl"))
    os.makedirs(export_dir, exist_ok=True)

    filename = f"{coll_name.lower()}{f'_{suffix}' if suffix else ''}.stl"
    bpy.ops.wm.stl_export(filepath=os.path.join(export_dir, filename), export_selected_objects=True)

    bpy.data.objects.remove(merged_obj, do_unlink=True)
    bpy.data.meshes.remove(merged_mesh)

class OBJECT_OT_Krteq_export(bpy.types.Operator):
    bl_idname = "object.krteq_export"
    bl_label = "Export STL"
    bl_description = "Automated Krteq STL export"

    def execute(self, context):
        if context.active_object and context.active_object.mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')

        original_selection = context.selected_objects
        original_active = context.active_object
        original_use_keychron = context.scene.use_keychron_stab

        bpy.context.view_layer.objects.active = None
        for scene_obj in bpy.context.view_layer.objects:
            scene_obj.select_set(False)

        context.scene.use_keychron_stab = False
        export_stl(self, "Top", rotation=(180.0, 0.0, 0.0))
        export_stl(self, "Bottom", clear_rotation=False)
        export_stl(self, "LED")

        context.scene.use_keychron_stab = True
        export_stl(self, "Top", rotation=(180.0, 0.0, 0.0), suffix="keychron")
        export_stl(self, "Bottom", clear_rotation=False, suffix="keychron")

        context.scene.use_keychron_stab = original_use_keychron

        for obj in original_selection:
            obj.select_set(True)
        if original_active:
            context.view_layer.objects.active = original_active
                
        self.report({'INFO'}, "STL export complete")
        return {'FINISHED'}

class VIEW3D_PT_Krteq_panel(bpy.types.Panel):
    bl_label = "Krteq"
    bl_idname = "VIEW3D_PT_Krteq_panel"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Krteq'

    def draw(self, context):
        self.layout.prop(context.scene, "use_keychron_stab")
        self.layout.operator("object.krteq_export", icon='FILE_3D')

def register():
    bpy.types.Scene.use_keychron_stab = bpy.props.BoolProperty(
        name="Keychron stab", description="Enable Keychron stabilizer", default=False
    )
    bpy.utils.register_class(OBJECT_OT_Krteq_export)
    bpy.utils.register_class(VIEW3D_PT_Krteq_panel)

def unregister():
    if hasattr(bpy.types.Scene, "use_keychron_stab"):
        del bpy.types.Scene.use_keychron_stab
    bpy.utils.unregister_class(OBJECT_OT_Krteq_export)
    bpy.utils.unregister_class(VIEW3D_PT_Krteq_panel)

if __name__ == "__main__":
    register()