# Builds edges.png: the same 64x48 pixel region (sphere outline + floor/wall edge), enlarged 6x without smoothing,
# for centre_1spp, jitter 1/4/16/64 spp and the reference. Standard library only.
# Usage: python3 -I experiments/2026-10-15_sampler-aa/make_montage.py
import struct, zlib, os
here = os.path.dirname(os.path.abspath(__file__))
X0, Y0, W, H, K = 196, 186, 64, 48, 6
names = ["centre_1spp", "jitter_1spp", "jitter_4spp", "jitter_16spp", "jitter_64spp", "reference"]

def read_pfm(path):
    d = open(path, "rb").read()
    parts = d.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    vals = struct.unpack("<%df" % (w * h * 3), parts[3][: w * h * 12])
    return w, h, vals

def enc(v):                       # same as Display::encode with exposure 1, gamma 2.2
    v = min(1.0, max(0.0, v))
    return int(v ** (1 / 2.2) * 255 + 0.5)

def crop(name):
    w, h, vals = read_pfm(os.path.join(here, name + ".pfm"))
    rows = []
    for y in range(Y0, Y0 + H):
        row = []
        for x in range(X0, X0 + W):
            i = ((h - 1 - y) * w + x) * 3          # PFM rows are stored bottom to top
            row.append((enc(vals[i]), enc(vals[i + 1]), enc(vals[i + 2])))
        rows.append(row)
    return rows

cols, gap = 3, 8
cw, ch = W * K, H * K
tiles = [crop(n) for n in names]
total_w = cols * cw + (cols - 1) * gap
total_h = 2 * ch + gap
img = [[(255, 255, 255)] * total_w for _ in range(total_h)]
for t, tile in enumerate(tiles):
    ox, oy = (t % cols) * (cw + gap), (t // cols) * (ch + gap)
    for y in range(ch):
        for x in range(cw):
            img[oy + y][ox + x] = tile[y // K][x // K]

raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in img)
def chunk(t, d):
    c = struct.pack(">I", len(d)) + t + d
    return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", total_w, total_h, 8, 2, 0, 0, 0)) \
      + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
open(os.path.join(here, "edges.png"), "wb").write(png)
print("wrote edges.png", total_w, "x", total_h)

# The same crops as separate files in docs/screenshots (referenced from docs/WORKLOG.md)
def write_png(path, rows):
    w, h = len(rows[0]), len(rows)
    raw = b"".join(b"\x00" + bytes(c for px in row for c in px) for row in rows)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) \
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)

shots = os.path.join(here, "..", "..", "docs", "screenshots")
labels = {"centre_1spp": "centre-1spp", "jitter_1spp": "jitter-1spp", "jitter_4spp": "jitter-4spp",
          "jitter_16spp": "jitter-16spp", "jitter_64spp": "jitter-64spp", "reference": "reference-1024spp"}
for n, tile in zip(names, tiles):
    big = [[tile[y // K][x // K] for x in range(cw)] for y in range(ch)]
    write_png(os.path.join(shots, "2026-10-15_aa-crop-%s.png" % labels[n]), big)
print("wrote", len(names), "crops to docs/screenshots")
