"""Export the Blender scene (room + VW Beetle) to Scene/Test/Beetle_Room.glb for the ray tracer.

Run inside Blender (Scripting tab, or the MCP execute_blender_code). The ray tracer has no texture support, so
for the export only, every beetle part gets a plain-factor stand-in material (colour, metallic, roughness, transmission);
the original materials are put back afterwards and the stand-ins deleted. The factors are guesses from how each part
looks in Blender, not measured values. The area light needs export_lights=True so that its node (with the rt_* extras) is written.
Assumes the collection "Beetle" (parts parented to the empty "Point001") and the material "Paint_SunflowerYellow".
Beetle_Room.glb contains the purchased CGTrader model, so it is git-ignored.
"""
import os

import bpy

# next to the saved .blend (Scene/Test/Untitled.blend in this repository)
OUT = os.path.join(os.path.dirname(bpy.data.filepath), "Beetle_Room.glb")


def mk(name, color, metal=0.0, rough=0.5, trans=0.0, ior=1.5):
    m = bpy.data.materials.new("RT_" + name)
    m.use_nodes = True
    b = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    b.inputs['Base Color'].default_value = (*color, 1)
    b.inputs['Metallic'].default_value = metal
    b.inputs['Roughness'].default_value = rough
    b.inputs['Transmission Weight'].default_value = trans
    b.inputs['IOR'].default_value = ior
    return m


M = {
    'chrome': mk('chrome', (0.9, 0.9, 0.9), 1.0, 0.08), 'chrome2': mk('chrome_soft', (0.8, 0.8, 0.8), 1.0, 0.25),
    'metalmatte': mk('metalmatte', (0.55, 0.55, 0.55), 1.0, 0.45), 'metalblack': mk('metalblack', (0.04, 0.04, 0.04), 0.8, 0.4),
    'rubber': mk('rubber', (0.02, 0.02, 0.02), 0.0, 0.8), 'wheel': mk('wheel', (0.75, 0.75, 0.72), 0.0, 0.3),
    'plastic': mk('plastic', (0.03, 0.03, 0.03), 0.0, 0.5), 'cloth': mk('cloth', (0.38, 0.22, 0.1), 0.0, 0.9),
    'carpet': mk('carpet', (0.25, 0.2, 0.14), 0.0, 1.0), 'interior': mk('interior', (0.45, 0.36, 0.26), 0.0, 0.6),
    'engine': mk('engine', (0.15, 0.15, 0.15), 0.6, 0.5), 'glass': mk('glass', (0.93, 0.97, 0.95), 0.0, 0.02, 1.0, 1.45),
    'lens': mk('headlight', (1, 1, 1), 0.0, 0.03, 1.0, 1.5), 'paint': mk('paint', (0.888, 0.479, 0.002), 0.0, 0.25),
}
BY_NAME = {
    'Tire': 'rubber', 'Caoutchouc': 'rubber', 'Wheel': 'wheel', 'Metal': 'chrome', 'MetalMatte': 'metalmatte',
    'MetalNoir': 'metalblack', 'Plastic': 'plastic', 'Volant': 'plastic', 'Grille': 'metalblack', 'Tissu': 'cloth',
    'Carpet': 'carpet', 'Engine': 'engine', 'Phare': 'lens', 'avant_phare': 'chrome', 'Miror': 'chrome',
    'Object023': 'chrome', 'Vitre': 'glass', 'Left_Door_006': 'glass', 'Right_Door_004': 'glass',
    'Back_Hood_000': 'rubber', 'Back_Hood_002': 'chrome2', 'Back_Hood_003': 'plastic', 'Front_Hood_001': 'chrome2',
    'Front_Hood_002': 'rubber', 'Left_Door_001': 'interior', 'Right_Door_000': 'interior', 'Left_Door_002': 'chrome2',
    'Left_Door_003': 'chrome2', 'Right_Door_001': 'chrome2', 'Right_Door_002': 'chrome2', 'Left_Door_004': 'chrome',
    'Left_Door_005': 'chrome', 'Right_Door_003': 'chrome',
}

parts = [o for o in bpy.data.collections["Beetle"].objects if o.type == 'MESH']
saved = {o.name: o.data.materials[0] for o in parts}
for o in parts:
    key = 'paint' if saved[o.name].name == 'Paint_SunflowerYellow' else BY_NAME[o.name.strip()]
    o.data.materials[0] = M[key]
try:
    bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', export_cameras=True, export_lights=True,
                              export_extras=True, export_apply=True, export_yup=True, export_image_format='NONE')
finally:
    for o in parts:
        o.data.materials[0] = saved[o.name]
    for m in [m for m in bpy.data.materials if m.name.startswith("RT_")]:
        bpy.data.materials.remove(m)
