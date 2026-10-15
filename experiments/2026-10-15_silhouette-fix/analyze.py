# -*- coding: utf-8 -*-
# Compares legacy and fixed silhouette handling: counts of changed / black pixels, and a before/after montage.
# Standard library only.  Usage: python3 -I experiments/2026-10-15_silhouette-fix/analyze.py
import os, struct, zlib
here = os.path.dirname(os.path.abspath(__file__))

def read_pfm(path):
    d = open(path, "rb").read()
    parts = d.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, struct.unpack("<%df" % (w * h * 3), parts[3][: w * h * 12])

def stats(name_a, name_b):
    w, h, a = read_pfm(os.path.join(here, name_a + ".pfm"))
    _, _, b = read_pfm(os.path.join(here, name_b + ".pfm"))
    n = w * h
    changed = big = dark_a = dark_b = 0
    maxd = 0.0
    sumsq = 0.0
    for i in range(n):
        pa, pb = a[i * 3:i * 3 + 3], b[i * 3:i * 3 + 3]
        if pa != pb:
            changed += 1
            d = max(abs(pa[k] - pb[k]) for k in range(3))
            maxd = max(maxd, d)
            big += d > 0.1
        dark_a += pa == (0.0, 0.0, 0.0)
        dark_b += pb == (0.0, 0.0, 0.0)
        sumsq += sum((pa[k] - pb[k]) ** 2 for k in range(3))
    return dict(n=n, changed=changed, big=big, black_legacy=dark_a, black_fixed=dark_b, maxd=maxd, rmse=(sumsq / (3 * n)) ** 0.5)

lines = ["legacy (M0) vs fixed silhouette handling: pixels that differ (exact comparison of the linear film)", ""]
for label, a, b in (("Room_2Spheres, 1 spp, pixel centre, point light", "Room_2Spheres_1spp_legacy", "Room_2Spheres_1spp_fixed"),
                    ("Untitled, 1 spp, pixel centre, point light", "Untitled_1spp_legacy", "Untitled_1spp_fixed"),
                    ("Untitled, 64 spp, jitter, area light (the latest screenshot)", "Untitled_64spp_legacy", "Untitled_64spp_fixed")):
    s = stats(a, b)
    lines.append("%s" % label)
    lines.append("  changed pixels: %d of %d (%.3f%%), changed by more than 0.1: %d, largest change: %.3f, RMSE: %.6f"
                 % (s["changed"], s["n"], 100.0 * s["changed"] / s["n"], s["big"], s["maxd"], s["rmse"]))
    lines.append("  exactly black pixels: legacy %d -> fixed %d" % (s["black_legacy"], s["black_fixed"]))
    lines.append("")
open(os.path.join(here, "results.txt"), "w", encoding="utf-8").write("\n".join(lines))
print("\n".join(lines))

# ---- before/after montage: 6x enlarged crops around the outline of the metal sphere and of the glass sphere
K, CW, CH = 6, 48, 36
regions = [("metal sphere, left outline", 175, 222), ("glass sphere, right outline", 421, 222)]
def enc(v):
    v = min(1.0, max(0.0, v)); return int(v ** (1 / 2.2) * 255 + 0.5)
def crop(name, x0, y0):
    w, h, v = read_pfm(os.path.join(here, name + ".pfm"))
    return [[tuple(enc(v[((h - 1 - y) * w + x) * 3 + c]) for c in range(3)) for x in range(x0, x0 + CW)] for y in range(y0, y0 + CH)]
gap = 6
tw, th = CW * K, CH * K
total_w, total_h = 2 * tw + gap, len(regions) * th + (len(regions) - 1) * gap
img = [[(255, 255, 255)] * total_w for _ in range(total_h)]
for r, (_, x0, y0) in enumerate(regions):
    for c, name in enumerate(("Room_2Spheres_1spp_legacy", "Room_2Spheres_1spp_fixed")):
        tile = crop(name, x0, y0)
        ox, oy = c * (tw + gap), r * (th + gap)
        for y in range(th):
            for x in range(tw):
                img[oy + y][ox + x] = tile[y // K][x // K]
raw = b"".join(b"\x00" + bytes(ch for px in row for ch in px) for row in img)
def chunk(t, d):
    c = struct.pack(">I", len(d)) + t + d
    return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", total_w, total_h, 8, 2, 0, 0, 0)) \
      + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
open(os.path.join(here, "before_after.png"), "wb").write(png)
open(os.path.join(here, "..", "..", "docs", "screenshots", "2026-10-15_silhouette-fix-before-after.png"), "wb").write(png)
print("wrote before_after.png (%d x %d)" % (total_w, total_h))
