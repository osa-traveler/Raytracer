#!/usr/bin/env python3
"""Compares this renderer's PBR render with Blender Cycles' render of the same scene: mean RGB of every sphere (the central half of its disc),
and the ratio ours / Cycles. Usage: python3 experiments/analyze_m7_cycles.py MATERIALS_DIR CYCLES_DIR (writes CYCLES_DIR/results.txt)"""
import array, json, os, sys
mdir, cdir = sys.argv[1], sys.argv[2]
W, H = 640, 480
def pfm(p):
    d = open(p, "rb").read(); parts = d.split(b"\n", 3); a = array.array("f"); a.frombytes(parts[3]); return a
def at(a, x, y): i = ((H - 1 - y) * W + x) * 3; return a[i], a[i + 1], a[i + 2]
def disc(a, cx, cy, r):
    s = [0.0, 0.0, 0.0]; n = 0
    for y in range(int(cy - r), int(cy + r) + 1):
        for x in range(int(cx - r), int(cx + r) + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r: p = at(a, x, y); n += 1; s = [s[k] + p[k] for k in range(3)]
    return [v / n for v in s]
ours = pfm(os.path.join(mdir, "pbr_1024spp.pfm"))
cyc = json.load(open(os.path.join(cdir, "cycles_1024spp.json")))
centres = json.load(open(os.path.join(mdir, "sphere_centres.json")))
names = {"gold": ["gold r=%s" % r for r in ("0.05", "0.1", "0.2", "0.35", "0.5", "0.75", "1.0")],
         "plastic": ["plastic r=%s" % r for r in ("0.05", "0.1", "0.2", "0.35", "0.5", "0.75", "1.0")],
         "front": ["glass ior 1.2", "glass ior 1.5", "glass ior 2.4", "mirror", "grey mirror"]}
lines = ["%-16s %-22s %-22s %s" % ("sphere", "ours R G B", "Cycles R G B", "ours / Cycles (R, G, B)")]
rows = []
for k, lst in centres.items():
    for (cx, cy, r), name, c in zip(lst, names[k], cyc["spheres"][k]):
        o = disc(ours, cx, cy, 0.5 * r)
        ratio = [o[i] / c[i] for i in range(3)]
        lines.append("%-16s %.3f %.3f %.3f     %.3f %.3f %.3f     %.3f %.3f %.3f" % (name, *o, *c, *ratio))
        rows.append(dict(name=name, ours=o, cycles=c, ratio=ratio, group=k))
lines.append("")
lines.append("Cycles: Blender %s, %d spp" % (cyc["blender"], cyc["samples"]))
open(os.path.join(cdir, "results.txt"), "w").write("\n".join(lines) + "\n")
json.dump(rows, open(os.path.join(cdir, "results.json"), "w"), indent=1)
print("\n".join(lines))
