"""Renders a glb scene of this renderer in Blender Cycles, for comparison (M7). Run it in a SEPARATE background Blender process:
    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python tools/cycles_render.py -- OUT_PREFIX [SAMPLES]
It never touches a Blender that is open. The scene is Scene/Test/MaterialRow.glb as the renderer sees it:
  * materials from the glTF importer (Principled BSDF: base colour, metallic, roughness, transmission, IOR, specular);
  * the area light as an emissive plane (emission strength = rt_radiance, so its radiance is the same number);
  * the sky of the renderer (`background()`: mix((.9,.9,.9), (.4,.6,.9), 0.5 (y + 1)), times 0.5) as the world;
  * camera from the glb; 640x480; box pixel filter of width 1; no denoising; 6 bounces; Standard view transform.
Writes OUT_PREFIX.exr (linear float), OUT_PREFIX.png and OUT_PREFIX.json (mean RGB of the spheres and the rows).
"""
import json, math, sys
import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
out = argv[0]; samples = int(argv[1]) if len(argv) > 1 else 512
GLB = "Scene/Test/MaterialRow.glb"
LIGHT_SIZE = (3.2, 1.2); LIGHT_POS_GLTF = (0.0, 4.2, 1.2); RADIANCE = 7.0; SKY = 0.5

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=GLB)
scene = bpy.context.scene

# light: an emissive rectangle facing down. glTF (x, y, z) -> Blender (x, -z, y).
lx, ly, lz = LIGHT_POS_GLTF[0], -LIGHT_POS_GLTF[2], LIGHT_POS_GLTF[1]
mesh = bpy.data.meshes.new("LightPlane")
sx, sy = LIGHT_SIZE[0] / 2, LIGHT_SIZE[1] / 2
mesh.from_pydata([(-sx, -sy, 0), (sx, -sy, 0), (sx, sy, 0), (-sx, sy, 0)], [], [(0, 3, 2, 1)])     # normal -Z (down)
obj = bpy.data.objects.new("LightPlane", mesh); obj.location = (lx, ly, lz); scene.collection.objects.link(obj)
mat = bpy.data.materials.new("LightEmission"); mat.use_nodes = True
nt = mat.node_tree; nt.nodes.clear()
em = nt.nodes.new("ShaderNodeEmission"); em.inputs["Color"].default_value = (1, 1, 1, 1); em.inputs["Strength"].default_value = RADIANCE
outn = nt.nodes.new("ShaderNodeOutputMaterial"); nt.links.new(em.outputs[0], outn.inputs["Surface"])
mesh.materials.append(mat)

# world: the renderer's sky gradient. Texture coordinate "Object" is the ray direction in a world shader.
world = bpy.data.worlds.new("Sky"); world.use_nodes = True; scene.world = world
wt = world.node_tree; wt.nodes.clear()
tc = wt.nodes.new("ShaderNodeTexCoord"); sep = wt.nodes.new("ShaderNodeSeparateXYZ")
t = wt.nodes.new("ShaderNodeMath"); t.operation = "MULTIPLY_ADD"; t.inputs[1].default_value = 0.5; t.inputs[2].default_value = 0.5
mix = wt.nodes.new("ShaderNodeMix"); mix.data_type = "RGBA"
mix.inputs[6].default_value = (0.9, 0.9, 0.9, 1.0); mix.inputs[7].default_value = (0.4, 0.6, 0.9, 1.0)
bg = wt.nodes.new("ShaderNodeBackground"); bg.inputs["Strength"].default_value = SKY
wo = wt.nodes.new("ShaderNodeOutputWorld")
wt.links.new(tc.outputs["Object"], sep.inputs[0]); wt.links.new(sep.outputs["Z"], t.inputs[0])
wt.links.new(t.outputs[0], mix.inputs[0]); wt.links.new(mix.outputs[2], bg.inputs["Color"]); wt.links.new(bg.outputs[0], wo.inputs["Surface"])

cam = next(o for o in scene.objects if o.type == "CAMERA"); scene.camera = cam
scene.render.engine = "CYCLES"
c = scene.cycles
c.device = "CPU"; c.samples = samples; c.use_denoising = False; c.use_adaptive_sampling = False
c.pixel_filter_type = "BOX"; c.filter_width = 1.0
c.max_bounces = 6; c.diffuse_bounces = 6; c.glossy_bounces = 6; c.transmission_bounces = 6; c.volume_bounces = 0; c.transparent_max_bounces = 6
c.sample_clamp_direct = 0; c.sample_clamp_indirect = 0
c.caustics_reflective = True; c.caustics_refractive = True
scene.render.resolution_x = 640; scene.render.resolution_y = 480; scene.render.resolution_percentage = 100
scene.view_settings.view_transform = "Standard"; scene.view_settings.look = "None"
scene.render.image_settings.file_format = "OPEN_EXR"; scene.render.image_settings.color_depth = "32"
scene.render.filepath = out + ".exr"
bpy.ops.render.render(write_still=True)

# measure from the linear image
img = bpy.data.images.load(out + ".exr")
W, H = img.size
px = list(img.pixels)                                           # RGBA, rows bottom to top
def at(x, y): i = ((H - 1 - y) * W + x) * 4; return px[i], px[i + 1], px[i + 2]
def mean_disc(cx, cy, r):
    s = [0.0, 0.0, 0.0]; n = 0
    for y in range(int(cy - r), int(cy + r) + 1):
        for x in range(int(cx - r), int(cx + r) + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r and 0 <= x < W and 0 <= y < H:
                p = at(x, y)
                for k in range(3): s[k] += p[k]
                n += 1
    return [v / n for v in s]
centres = json.load(open("experiments/2026-10-16_m7-materials/sphere_centres.json"))
res = {"samples": samples, "blender": bpy.app.version_string, "spheres": {}}
for name, lst in centres.items():
    res["spheres"][name] = [mean_disc(cx, cy, 0.5 * r) for cx, cy, r in lst]
json.dump(res, open(out + ".json", "w"), indent=1)
scene.render.image_settings.file_format = "PNG"; scene.render.image_settings.color_depth = "8"
img.filepath_raw = out + ".png"; img.file_format = "PNG"; img.save_render(out + ".png", scene=scene)
print("DONE", out)
