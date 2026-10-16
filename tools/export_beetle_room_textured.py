"""Export the Blender scene (room + VW Beetle) to a glb WITH textures for the ray tracer (M8, step 1).

Run inside Blender (Scripting tab, or the MCP execute_blender_code); the .blend must be saved inside the repository
(Scene/Test/...). Unlike tools/export_beetle_room.py (single-colour stand-in materials) this keeps the beetle's real texture maps.

glTF has no UDIM tiles, and the beetle's UVs span 2 x 3 tiles (u 0..2, v 0..3: tiles 1001, 1002, 1011, 1012, 1021, 1022). So:
  * the six 4096 x 4096 tiles of each map are shrunk to 2048 x 2048 and pasted into one 4096 x 6144 atlas
    (Assets/bettle/textures/atlas/: beetle_base_atlas.png = BaseColor, beetle_mr_atlas.png = glTF packing G = roughness, B = metalness);
    plus beetle_normal_atlas.png = the tangent-space normal maps (OpenGL convention, +Y up: what Blender's Normal Map node expects);
    the files are reused when they exist (delete them to rebuild);
  * for the export only, every textured part's UVs are divided by (2, 3) so they address the atlas, and the parts get a material
    that reads the atlases (paint parts: the same, multiplied by the sunflower yellow). The renderer computes its own vertex
    tangents from the UVs, so no TANGENT attribute is needed. Glass (material "Vitre") is left as it is.
Everything (UVs, materials, temporary images) is put back afterwards. When this script is run with exec() from another script, give it its own
namespace (exec(src, {})): it rebinds names such as m and old, which broke a caller once. The glb contains the purchased CGTrader model: git-ignored.
"""
import os

import bpy
import numpy as np

OUT_NAME = "Beetle_Room_rear_tex.glb"
T = 2048                                   # size of one tile in the atlas
YELLOW = (0.888, 0.479, 0.002, 1.0)        # linear, same as Paint_SunflowerYellow

blend_dir = os.path.dirname(bpy.data.filepath)
repo = os.path.dirname(os.path.dirname(blend_dir))
tile_dir = os.path.join(repo, "Assets", "bettle", "textures", "TEXTURE_HD")
atlas_dir = os.path.join(repo, "Assets", "bettle", "textures", "atlas")
OUT = os.path.join(blend_dir, OUT_NAME)
os.makedirs(atlas_dir, exist_ok=True)
TILES = [(i, j) for j in range(3) for i in range(2)]           # tile 1001 + i + 10 * j


def read_tile(kind, i, j):
    """One tile as raw 8-bit numbers (no colour conversion), Blender's row order (row 0 = bottom)."""
    im = bpy.data.images.load(os.path.join(tile_dir, f"VRAIBONRENDU_{kind}.CARHERBIE.{1001 + i + 10 * j}.png"), check_existing=False)
    im.colorspace_settings.name = 'Non-Color'
    h, w = im.size[1], im.size[0]
    a = np.empty(w * h * 4, dtype=np.float32)
    im.pixels.foreach_get(a)
    bpy.data.images.remove(im)
    a = (a.reshape(h, w, 4)[:, :, :3] * 255 + 0.5).astype(np.float32)
    return a.reshape(T, h // T, T, w // T, 3).mean(axis=(1, 3))      # 2 x 2 box filter


def save_png(path, rgba):
    im = bpy.data.images.new("atlas_tmp", rgba.shape[1], rgba.shape[0], alpha=False)
    im.colorspace_settings.name = 'Non-Color'
    im.pixels.foreach_set((rgba / 255.0).astype(np.float32).ravel())
    im.filepath_raw = path
    im.file_format = 'PNG'
    im.save()
    bpy.data.images.remove(im)


base_path = os.path.join(atlas_dir, "beetle_base_atlas.png")
mr_path = os.path.join(atlas_dir, "beetle_mr_atlas.png")
normal_path = os.path.join(atlas_dir, "beetle_normal_atlas.png")
if not os.path.exists(normal_path):
    nm = np.full((3 * T, 2 * T, 4), 255, dtype=np.float32)
    nm[:, :, :3] = (128, 128, 255)                                    # a flat normal where a tile is missing
    for i, j in TILES:
        nm[j * T:(j + 1) * T, i * T:(i + 1) * T, :3] = read_tile("Normal", i, j)   # (the box filter averages the encoded vectors; the renderer renormalises)
    save_png(normal_path, nm)
    del nm
if not (os.path.exists(base_path) and os.path.exists(mr_path)):
    base = np.full((3 * T, 2 * T, 4), 255, dtype=np.float32)
    mr = np.full((3 * T, 2 * T, 4), 255, dtype=np.float32)
    mr[:, :, 0] = 0
    for i, j in TILES:
        sl = (slice(j * T, (j + 1) * T), slice(i * T, (i + 1) * T))
        base[sl[0], sl[1], :3] = read_tile("BaseColor", i, j)
        mr[sl[0], sl[1], 1] = read_tile("Roughness", i, j)[:, :, 0]       # G = roughness
        mr[sl[0], sl[1], 2] = read_tile("Metalness", i, j)[:, :, 0]       # B = metalness
    save_png(base_path, base)
    save_png(mr_path, mr)
    del base, mr


def load_atlas(path, colorspace):
    im = bpy.data.images.load(path, check_existing=False)
    im.colorspace_settings.name = colorspace
    return im


img_base = load_atlas(base_path, 'sRGB')
img_mr = load_atlas(mr_path, 'Non-Color')
img_normal = load_atlas(normal_path, 'Non-Color')


def atlas_material(name, tint=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    tb = nt.nodes.new('ShaderNodeTexImage'); tb.image = img_base
    tm = nt.nodes.new('ShaderNodeTexImage'); tm.image = img_mr
    sep = nt.nodes.new('ShaderNodeSeparateColor')
    nt.links.new(tm.outputs['Color'], sep.inputs['Color'])
    nt.links.new(sep.outputs['Green'], bsdf.inputs['Roughness'])
    nt.links.new(sep.outputs['Blue'], bsdf.inputs['Metallic'])
    tn = nt.nodes.new('ShaderNodeTexImage'); tn.image = img_normal
    nmap = nt.nodes.new('ShaderNodeNormalMap'); nmap.space = 'TANGENT'
    nt.links.new(tn.outputs['Color'], nmap.inputs['Color'])
    nt.links.new(nmap.outputs['Normal'], bsdf.inputs['Normal'])
    if tint is None:
        nt.links.new(tb.outputs['Color'], bsdf.inputs['Base Color'])
    else:
        mix = nt.nodes.new('ShaderNodeMix'); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'
        mix.inputs['Factor'].default_value = 1.0
        mix.inputs['B'].default_value = tint
        nt.links.new(tb.outputs['Color'], mix.inputs['A'])
        nt.links.new(mix.outputs['Result'], bsdf.inputs['Base Color'])
    return m


mat_plain = atlas_material("RT_atlas")
mat_paint = atlas_material("RT_atlas_paint", YELLOW)

parts = [o for o in bpy.data.collections["Beetle"].objects if o.type == 'MESH']
saved_mat = {o.name: o.data.materials[0] for o in parts}
saved_uv = {}
try:
    for o in parts:
        old = saved_mat[o.name]
        if old.name == 'Vitre':
            continue                                              # glass keeps its own material (no texture)
        o.data.materials[0] = mat_paint if old.name == 'Paint_SunflowerYellow' else mat_plain
        uv = o.data.uv_layers.active.data
        a = np.empty(len(uv) * 2, dtype=np.float32)
        uv.foreach_get('uv', a)
        saved_uv[o.name] = a.copy()
        a = a.reshape(-1, 2)
        a[:, 0] /= 2.0
        a[:, 1] /= 3.0
        uv.foreach_set('uv', a.ravel())
        o.data.update()
    bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', export_cameras=True, export_lights=True,
                              export_extras=True, export_apply=True, export_yup=True, export_image_format='AUTO')
finally:
    for o in parts:
        o.data.materials[0] = saved_mat[o.name]
        if o.name in saved_uv:
            o.data.uv_layers.active.data.foreach_set('uv', saved_uv[o.name])
            o.data.update()
    for m in (mat_plain, mat_paint):
        bpy.data.materials.remove(m)
    for im in (img_base, img_mr, img_normal):
        bpy.data.images.remove(im)
