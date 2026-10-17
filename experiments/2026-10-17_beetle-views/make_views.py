#!/usr/bin/env python3
"""Renders the beetle from five directions with the ray tracer. Only the camera of Scene/Test/Beetle_Room_rear_tex.glb is replaced
(position and rotation of its camera node), so Blender is not needed. Needs the glb (tools/export_beetle_room_textured.py) and
build-release/raytrace. Usage, from the repository root: python3 experiments/2026-10-17_beetle-views/make_views.py [spp [views [preset]]]
spp defaults to 256; views = the numbers of the views to render, e.g. 1 or 135 (default: all). With an spp other than 256 the files get an _<spp>spp suffix.
With a preset (presets/NAME.json, see src/preset.h) ALL the render settings and the display exposure / gamma come from the preset (spp is ignored), the files are
written to best/ with the preset name as suffix, and only the camera of each view differs."""
import json, math, struct, subprocess, sys

SRC = "Scene/Test/Beetle_Room_rear_tex.glb"
OUT = "experiments/2026-10-17_beetle-views/"
SPP = sys.argv[1] if len(sys.argv) > 1 else "256"
ONLY = sys.argv[2] if len(sys.argv) > 2 else "12345"
PRESET = sys.argv[3] if len(sys.argv) > 3 else ""
TAG = "_" + PRESET if PRESET else ("" if SPP == "256" else "_%sspp" % SPP)
OUTDIR = OUT + ("best/" if PRESET else "")

# The car in glTF coordinates (Y up), measured in Blender after the manual adjustment and converted with (x, y, z) -> (x, z, -y):
# body centre, the direction of the front (engine -> body), the sideways direction, and the two glass spheres (centre, radius).
# 2026-10-17: body centre (-1.27, 0.31) and front (0.61, 0.79) in Blender's x, y; the car is 6.1 m long, 2.3 m wide and 2.25 m high.
C = (-1.27, 1.0, -0.31)
F = (0.61, 0.0, -0.79)
S = (0.79, 0.0, 0.61)
SPHERES = [((2.5, 1.0, 0.0), 1.0), ((-3.94, 0.35, 2.14), 0.35)]
LIMIT = 6.3                                    # keep the camera inside the 14 m room

# (name, angle around the car from its front towards S in degrees, distance on the floor, height, vertical field of view)
VIEWS = [
    ("1_front_three_quarter", 20, 6.5, 1.5, 0.56),
    ("2_side", 105, 7.0, 1.2, 0.56),
    ("3_rear_three_quarter", 215, 5.0, 1.6, 0.62),
    ("4_high_from_front", 60, 5.5, 4.6, 0.62),
    ("5_low_front_close", -15, 5.5, 0.5, 0.80),
]

def look_at_quat(eye, target, up=(0, 1, 0)):
    z = [eye[i] - target[i] for i in range(3)]; l = math.sqrt(sum(c * c for c in z)); z = [c / l for c in z]
    x = [up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]]; l = math.sqrt(sum(c * c for c in x)); x = [c / l for c in x]
    y = [z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]]
    m = [[x[0], y[0], z[0]], [x[1], y[1], z[1]], [x[2], y[2], z[2]]]
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0:
        s = math.sqrt(t + 1) * 2; return [(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s]
    if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1 + m[0][0] - m[1][1] - m[2][2]) * 2; return [0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s]
    if m[1][1] > m[2][2]:
        s = math.sqrt(1 + m[1][1] - m[0][0] - m[2][2]) * 2; return [(m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s]
    s = math.sqrt(1 + m[2][2] - m[0][0] - m[1][1]) * 2; return [(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s]

d = open(SRC, "rb").read()
jlen = struct.unpack("<I", d[12:16])[0]
doc = json.loads(d[20:20 + jlen])
rest = d[20 + jlen:]                                             # the BIN chunk, unchanged
cam_node = next(n for n in doc["nodes"] if n.get("camera") is not None)

def blocked(eye):
    """True if a glass sphere sits (nearly) on the line of sight from the camera to the car."""
    d = [C[i] - eye[i] for i in range(3)]; n = math.sqrt(sum(c * c for c in d)); d = [c / n for c in d]
    for centre, r in SPHERES:
        v = [centre[i] - eye[i] for i in range(3)]
        t = sum(v[i] * d[i] for i in range(3))
        if 0 < t < n:
            dist = math.sqrt(max(0.0, sum(c * c for c in v) - t * t))
            if dist < r + 0.4: return True
    return False

def camera_position(ang, dist, height):
    a = math.radians(ang)
    px = C[0] + dist * (math.cos(a) * F[0] + math.sin(a) * S[0])
    pz = C[2] + dist * (math.cos(a) * F[2] + math.sin(a) * S[2])
    return (max(-LIMIT, min(LIMIT, px)), height, max(-LIMIT, min(LIMIT, pz)))

for name, ang, dist, height, yfov in VIEWS:
    if name[0] not in ONLY:
        continue
    eye = camera_position(ang, dist, height)
    for turn in (8, -8, 16, -16, 24, -24, 32, -32):                 # a sphere in the way: turn the camera around the car a little
        if not blocked(eye): break
        eye = camera_position(ang + turn, dist, height)
        print("   (a glass sphere was in the way, trying %+d degrees)" % turn, flush=True)
    cam_node.pop("matrix", None)
    cam_node["translation"] = list(eye)
    cam_node["rotation"] = look_at_quat(eye, (C[0], 0.95, C[2]))
    doc["cameras"][cam_node["camera"]]["perspective"]["yfov"] = yfov
    js = json.dumps(doc, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    glb = OUT + "out/" + name + ".glb"
    with open(glb, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + len(rest)))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js + rest)
    print(name, "camera at", [round(c, 2) for c in eye], flush=True)
    if PRESET:
        cmd = ["./build-release/raytrace", glb, "--headless", "--preset", PRESET, "--out", OUTDIR + name + TAG, "--note", "beetle view " + name + ", preset " + PRESET]
    else:
        cmd = ["./build-release/raytrace", glb, "--headless", "--integrator", "path", "--spp", SPP, "--jitter", "--max-depth", "8", "--rr",
               "--cull-backfaces", "--out", OUTDIR + name + TAG, "--note", "beetle view " + name]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
