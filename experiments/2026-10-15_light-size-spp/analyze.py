# 1. Penumbra width on the floor for each light size (from the 512 spp reference images).
# 2. size_x_spp.png: the same 80x60 pixel region (the metal sphere's shadow), 3x enlarged, light size (rows) x samples (columns).
# Standard library only.  Usage: python3 -I experiments/2026-10-15_light-size-spp/analyze.py
import json, math, os, struct, zlib
here = os.path.dirname(os.path.abspath(__file__))
sizes = ["0.25", "2", "4"]
spps = ["1", "4", "16", "64", "ref"]
W, H = 640, 480

def read_pfm(path):
    d = open(path, "rb").read()
    parts = d.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, struct.unpack("<%df" % (w * h * 3), parts[3][: w * h * 12])

def lum_at(v, w, h, x, y):
    i = ((h - 1 - y) * w + x) * 3
    return 0.2126 * v[i] + 0.7152 * v[i + 1] + 0.0722 * v[i + 2]

# ---- camera -> floor position of a pixel (the floor is the plane y = 0)
cam = json.load(open(os.path.join(here, "size2_ref.json")))["camera"]
pos, fwd, tan_half = cam["position"], cam["forward"], cam["tan_half_fov_y"]
def norm(a): l = math.sqrt(sum(c * c for c in a)); return [c / l for c in a]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
right = norm(cross(fwd, [0, 1, 0])); up = cross(right, fwd)
def floor_point(px, py):
    sx = (2 * px / W - 1) * (W / H) * tan_half
    sy = (1 - 2 * py / H) * tan_half
    d = norm([fwd[i] + right[i] * sx + up[i] * sy for i in range(3)])
    t = -pos[1] / d[1]
    return [pos[i] + d[i] * t for i in range(3)]

# ---- 10%-90% width of the shadow edge along the column x = 250
X = 250
out = ["Penumbra width on the floor, 10%%-90%% of the shadow-to-light brightness step, column x = %d (512 spp references)" % X,
       "light size [m]   width [px]   width on the floor [m]   width / size"]
rows = []
for size in sizes:
    w, h, v = read_pfm(os.path.join(here, "size%s_ref.pfm" % size))
    prof = [lum_at(v, w, h, X, y) for y in range(H)]
    shadow = sum(prof[292:301]) / 9
    lit = sum(prof[318:326]) / 8
    t = [(p - shadow) / (lit - shadow) for p in prof]
    def crossing(level):                      # first y (from 290 down) where t rises through `level`, linear interpolation
        for y in range(290, 326):
            if t[y] < level <= t[y + 1]:
                return y + (level - t[y]) / (t[y + 1] - t[y])
        return None
    y10, y90 = crossing(0.1), crossing(0.9)
    p10, p90 = floor_point(X + 0.5, y10 + 0.5), floor_point(X + 0.5, y90 + 0.5)
    meters = math.dist(p10, p90)
    rows.append((float(size), y90 - y10, meters))
    out.append("%-15s %-11.2f %-24.3f %.3f" % (size, y90 - y10, meters, meters / float(size)))
open(os.path.join(here, "penumbra.txt"), "w").write("\n".join(out) + "\n")
print("\n".join(out))

# ---- montage
X0, Y0, CW, CH, K = 206, 262, 80, 60, 3
def enc(v):
    v = min(1.0, max(0.0, v)); return int(v ** (1 / 2.2) * 255 + 0.5)
def crop(name):
    w, h, v = read_pfm(os.path.join(here, name + ".pfm"))
    return [[tuple(enc(v[((h - 1 - y) * w + x) * 3 + c]) for c in range(3)) for x in range(X0, X0 + CW)] for y in range(Y0, Y0 + CH)]
gap = 6
tw, th = CW * K, CH * K
total_w, total_h = len(spps) * tw + (len(spps) - 1) * gap, len(sizes) * th + (len(sizes) - 1) * gap
img = [[(255, 255, 255)] * total_w for _ in range(total_h)]
for r, size in enumerate(sizes):
    for c, spp in enumerate(spps):
        tile = crop("size%s_%s" % (size, "ref" if spp == "ref" else spp + "spp"))
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
open(os.path.join(here, "size_x_spp.png"), "wb").write(png)
shots = os.path.join(here, "..", "..", "docs", "screenshots")
open(os.path.join(shots, "2026-10-15_light-size-x-spp.png"), "wb").write(png)
print("wrote size_x_spp.png (%d x %d) and docs/screenshots/2026-10-15_light-size-x-spp.png" % (total_w, total_h))
