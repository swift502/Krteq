import bpy
import os
import math

# --- 1. YOUR EXPORT LOGIC ---
def export_specialized_collections(optional_rotation_degrees=(0.0, 0.0, 0.0)):
    # 1. Ensure the file is saved so we can establish the relative path
    if not bpy.data.filepath:
        print("ERROR: Please save your .blend file first to establish a path!")
        return

    # Setup export directory: {blend_file_path}/../production/stl/
    blend_dir = os.path.dirname(bpy.data.filepath)
    export_dir = os.path.abspath(os.path.join(blend_dir, "..", "production", "stl"))
    
    # Create the directories if they don't exist yet
    os.makedirs(export_dir, exist_ok=True)

    target_collections = ["Top", "Bottom", "LED"]

    # Save current mode and selection to restore later
    if bpy.context.active_object and bpy.context.active_object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
        
    original_selection = bpy.context.selected_objects
    original_active = bpy.context.active_object

    for coll_name in target_collections:
        coll = bpy.data.collections.get(coll_name)
        if not coll or not coll.objects:
            print(f"Skipping: Collection '{coll_name}' is missing or empty.")
            continue

        # 2. Identify the Root object
        root_obj_original = next((obj for obj in coll.objects if obj.name.startswith("Root")), None)
        
        if not root_obj_original:
            print(f"Skipping: No object starting with 'Root' found in '{coll_name}'.")
            continue

        # Deselect everything in the scene
        bpy.ops.object.select_all(action='DESELECT')

        # Select all objects in the current collection
        for obj in coll.objects:
            # Ensure objects are visible, otherwise bpy.ops might ignore them
            obj.hide_set(False) 
            obj.select_set(True)
        
        # Make the original root the active object
        bpy.context.view_layer.objects.active = root_obj_original

        # --- NON-DESTRUCTIVE WORKFLOW ---
        # Duplicate the selected objects. The duplicates automatically become the new selection.
        bpy.ops.object.duplicate()
        
        # The duplicated root is now our active object
        merged_obj = bpy.context.active_object
        
        # 3. Convert all selected (duplicated) objects to mesh (applies modifiers, converts curves, etc.)
        bpy.ops.object.convert(target='MESH')
        
        # 4. Join all selected (duplicated) objects into the duplicated root
        bpy.ops.object.join()

        # 5 & 6. Reset rotation and add optional rotation
        # By overwriting the rotation_euler, we inherently reset it and apply the new one.
        rot_rad = (
            math.radians(optional_rotation_degrees[0]),
            math.radians(optional_rotation_degrees[1]),
            math.radians(optional_rotation_degrees[2])
        )
        merged_obj.rotation_euler = rot_rad

        # Update the scene so the rotation is physically calculated before export
        bpy.context.view_layer.update()

        # 7. Export the merged object
        export_path = os.path.join(export_dir, f"{coll_name}.stl")
        
        try:
            # Standard legacy STL exporter (Blender 3.x and most 4.x setups)
            bpy.ops.export_mesh.stl(filepath=export_path, use_selection=True)
            print(f"Success: Exported {coll_name}.stl")
        except AttributeError:
            # Fallback for Blender 4.1+ if using the new C++ exporter natively
            bpy.ops.wm.stl_export(filepath=export_path, export_selected_objects=True)
            print(f"Success: Exported {coll_name}.stl (Blender 4.x+)")

        # 8. Cleanup: Delete the temporary merged object
        bpy.ops.object.delete()

    # Restore original selection state
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
            
    print(f"--- Batch export complete! Check {export_dir} ---")

# --- 2. YOUR OPERATOR (The Action) ---
class OBJECT_OT_custom_export(bpy.types.Operator):
    bl_idname = "object.custom_export"
    bl_label = "Export Specific Collections"
    bl_description = "Merges and exports Top, Bottom, and LED collections"

    def execute(self, context):
        # Call your function when the button is pressed
        export_specialized_collections()
        self.report({'INFO'}, "Batch export complete!")
        return {'FINISHED'}

# --- 3. YOUR UI PANEL (The Button) ---
class VIEW3D_PT_custom_export_panel(bpy.types.Panel):
    bl_label = "Production Exporter"
    bl_idname = "VIEW3D_PT_custom_export_panel"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Export Tool' # Creates a new tab named "Export Tool" in the N-Panel

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
        # Check if it's already registered to avoid errors during reloads
        if not hasattr(bpy.types, cls.__name__):
            bpy.utils.register_class(cls)

def unregister():
    for cls in reversed(classes):
        if hasattr(bpy.types, cls.__name__):
            bpy.utils.unregister_class(cls)

# This allows you to run it directly from VS Code if you use the Blender Development extension
if __name__ == "__main__":
    register()