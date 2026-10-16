#!/usr/bin/env python3
"""M7 material experiments: the error of BSDF-only / NEE / MIS renders against the reference, for the whole image and for the
three sphere rows (metal, plastic, glass + mirrors), and the brightness of the PBR render against the single-scattering one.
Usage: python3 experiments/analyze_m7.py DIR   (writes DIR/results.txt)"""
import array, json, math, os, sys
d = sys.argv[1]
W, H = 640, 480
def pfm(path):
    data = open(path, "rb").read(); parts = data.split(b"\n", 3)
    a = array.array("f"); a.frombytes(parts[3]); return a            # rows bottom-to-top, RGB
def px(a, x, y):                                                      # y = 0 at the top of the image
    i = ((H - 1 - y) * W + x) * 3; return a[i], a[i + 1], a[i + 2]
def rmse(a, b, box):
    x0, y0, x1, y1 = box; s = 0.0; n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            pa, pb = px(a, x, y), px(b, x, y)
            for c in range(3): s += (pa[c] - pb[c]) ** 2
            n += 3
    return math.sqrt(s / n)
def mean(a, box):
    x0, y0, x1, y1 = box; s = [0.0, 0.0, 0.0]; n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            p = px(a, x, y)
            for c in range(3): s[c] += p[c]
            n += 1
    return [v / n for v in s]
REGIONS = json.load(open(os.path.join(d, "regions.json")))           # name -> [x0, y0, x1, y1]
ref = pfm(os.path.join(d, "reference.pfm"))
lines = ["RMSE against the reference (PBR, MIS, 4096 spp, seed 99)", "%-8s %-6s %s" % ("method", "spp", "  ".join("%-8s" % k for k in ["whole"] + list(REGIONS)))]
out = {}
for method in ("bsdf", "nee", "mis"):
    for n in (16, 64, 256):
        a = pfm(os.path.join(d, f"{method}_spp{n}.pfm"))
        vals = [rmse(a, ref, (0, 0, W, H))] + [rmse(a, ref, b) for b in REGIONS.values()]
        lines.append("%-8s %-6d %s" % (method, n, "  ".join("%.4f  " % v for v in vals)))
        out[f"{method}_{n}"] = vals
lines.append("")
lines.append("Mean RGB of each region (MIS 1024 spp, with / without the multiple-scattering compensation):")
a = pfm(os.path.join(d, "pbr_1024spp.pfm")); b = pfm(os.path.join(d, "pbr_1024spp_single_scatter.pfm"))
for name, box in REGIONS.items():
    ma, mb = mean(a, box), mean(b, box)
    lines.append("  %-10s with %.4f %.4f %.4f   without %.4f %.4f %.4f   ratio %.3f" % (name, *ma, *mb, ma[0] / mb[0]))
open(os.path.join(d, "results.txt"), "w").write("\n".join(lines) + "\n")
json.dump(out, open(os.path.join(d, "results.json"), "w"), indent=1)
print("\n".join(lines))
