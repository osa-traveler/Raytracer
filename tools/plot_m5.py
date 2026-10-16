# Draws the M5 figures for docs/RENDERING_GUIDE.md (needs matplotlib and numpy; run after the M5 experiments, from the repository root):
#   2026-10-16_m5-bvh-split.png         how the median split and the SAH split a scene (2D illustration)
#   2026-10-16_m5-accel-comparison.png  render time and work per ray for every triangle search
#   2026-10-16_m5-threads.png           speed-up against the number of threads
import json
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

plt.rcParams["font.family"] = ["Hiragino Sans", "sans-serif"]
SURFACE, INK, MUTED, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#dcdbd6"
BLUE, ORANGE, AQUA, YELLOW, MAGENTA, GRAY = "#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#8d8c86"
OUT = "docs/screenshots/"

# ---------------------------------------------------------------- 1. median split vs SAH (2D)
rng = np.random.default_rng(4)
def clusters():
    a = rng.normal([2.2, 2.4], [0.55, 0.5], (110, 2)); b = rng.normal([8.2, 3.3], [0.4, 0.45], (50, 2))
    c = rng.uniform([0.2, 0.2], [9.8, 5.8], (14, 2))
    return np.vstack([a, b, c])
P = clusters()
H = 0.13                                               # half size of the small primitives
lo, hi = P - H, P + H

def box(idx): return lo[idx].min(0), hi[idx].max(0)
def perimeter(l, h): return 2 * ((h[0] - l[0]) + (h[1] - l[1]))
def median_split(idx):
    ext = P[idx].max(0) - P[idx].min(0); ax = int(np.argmax(ext))
    order = idx[np.argsort(P[idx][:, ax])]; m = len(order) // 2
    return ax, order[:m], order[m:]
def sah_split(idx, B=16):
    best = (1e18, None)
    for ax in (0, 1):
        c = P[idx][:, ax]; cmin, cext = c.min(), c.max() - c.min()
        if cext <= 0: continue
        b = np.minimum(B - 1, ((c - cmin) / cext * B).astype(int))
        for k in range(B - 1):
            L, R = idx[b <= k], idx[b > k]
            if len(L) == 0 or len(R) == 0: continue
            cost = 1 + (perimeter(*box(L)) * len(L) + perimeter(*box(R)) * len(R)) / perimeter(*box(idx))
            if cost < best[0]: best = (cost, (ax, L, R))
    return best[1]

def draw(ax, split, title, note):
    idx = np.arange(len(P))
    ax.set_xlim(-0.2, 10.2); ax.set_ylim(-0.2, 6.4); ax.set_aspect("equal"); ax.axis("off")
    for i in idx: ax.add_patch(Rectangle(lo[i], 2 * H, 2 * H, color=GRAY, lw=0))
    ax.add_patch(Rectangle(*box(idx)[0:1], *(box(idx)[1] - box(idx)[0]), fill=False, ec=INK, lw=1.4, ls=(0, (5, 3))))
    ax_, L, R = split(idx)
    cols = (BLUE, ORANGE)
    total = 0
    for part, col in zip((L, R), cols):
        l, h = box(part); total += perimeter(l, h) * len(part)
        ax.add_patch(Rectangle(l, *(h - l), fill=True, fc=col, alpha=0.13, ec=col, lw=2.2))
        # one more level
        a2, L2, R2 = (median_split if split is median_split else sah_split)(part) if len(part) > 4 else (0, part, part[:0])
        for sub in (L2, R2):
            if len(sub) == 0: continue
            l2, h2 = box(sub)
            ax.add_patch(Rectangle(l2, *(h2 - l2), fill=False, ec=col, lw=1.1, alpha=0.8))
    ax.set_title(title, color=INK, fontsize=14, fontweight="bold", pad=8)
    ax.text(0.5, -0.02, note + f"\n子ノードの箱の「周長 × 個数」の合計: {total / perimeter(*box(idx)):.0f}(親の周長を 1 として。小さいほど、探索が少ない)",
            transform=ax.transAxes, ha="center", va="top", color=MUTED, fontsize=10.5, linespacing=1.5)

fig, axs = plt.subplots(1, 2, figsize=(13.6, 5.0), facecolor=SURFACE)
draw(axs[0], median_split, "中央値で分割", "個数が半分ずつになる所で切る。密集した塊の中を切ってしまい、\n2つの箱が重なる(重なった所では、両方を調べる)")
draw(axs[1], sah_split, "SAH(表面積ヒューリスティック)で分割", "「箱の表面積 × 個数」の合計が最小になる所で切る。\n塊の間の何もない所で切れて、2つの箱が重ならない")
fig.suptitle("BVH の分割(2次元での説明図。灰色 = 三角形、太い枠 = 子ノードの箱、細い枠 = その次の分割)", color=INK, fontsize=13.5, y=0.99)
fig.tight_layout(rect=(0, 0.02, 1, 0.95), w_pad=2)
fig.savefig(OUT + "2026-10-16_m5-bvh-split.png", dpi=110, facecolor=SURFACE); plt.close(fig)

# ---------------------------------------------------------------- 2. render time and work per ray for every triangle search
rows = json.load(open("experiments/2026-10-16_m5-accel/results.json"))
scenes = [("cornell", "Cornell\n(30 三角形)\nパス + MIS 16 spp"), ("room", "Room_2Spheres\n(7,948)\nclassic 4 spp"), ("untitled", "Untitled\n(11,916)\nclassic 4 spp"), ("beetle1", "ビートル\n(301,716)\nclassic 1 spp")]
modes = [("none", "BVH なし(全三角形)", GRAY), ("median", "BVH・中央値", BLUE), ("median_ord", "BVH・中央値 + 近い子から", "#7fb0ec"),
         ("sah", "BVH・SAH", ORANGE), ("sah_ord", "BVH・SAH + 近い子から", "#f5a47f"), ("sah_ord_any", "SAH + 近い子から + any-hit(影)", MAGENTA)]
def get(scene, mode, key):
    for r in rows:
        if r["scene"] == scene and r["mode"] == mode: return r[key]
    return None
fig, axs = plt.subplots(1, 2, figsize=(15.5, 6.2), facecolor=SURFACE)
for ax, key, ylabel, fmt in ((axs[0], "ms", "1フレームの描画時間 [ms](対数)", "{:,.0f}"), (axs[1], "tris_per_ray", "1レイあたりの三角形の判定数(対数)", "{:,.1f}")):
    w = 0.13
    for k, (mode, label, col) in enumerate(modes):
        xs, ys = [], []
        for i, (scene, _) in enumerate(scenes):
            v = get(scene, mode, key)
            if v is None: continue
            xs.append(i + (k - 2.5) * w); ys.append(v)
        ax.bar(xs, ys, width=w * 0.92, color=col, label=label if key == "ms" else None, zorder=3)
        for x, y in zip(xs, ys): ax.text(x, y * 1.12, fmt.format(y), ha="center", va="bottom", fontsize=7.6, color=INK, rotation=90)
    ax.set_yscale("log"); ax.set_xticks(range(len(scenes))); ax.set_xticklabels([s[1] for s in scenes], fontsize=10, color=INK)
    ax.set_ylabel(ylabel, color=MUTED, fontsize=11); ax.set_facecolor(SURFACE)
    ax.grid(axis="y", color=GRID, lw=0.8, zorder=0); ax.set_axisbelow(True)
    for sp in ("top", "right"): ax.spines[sp].set_visible(False)
    for sp in ("left", "bottom"): ax.spines[sp].set_color(GRID)
    ax.tick_params(colors=MUTED)
    ax.set_ylim(top=ax.get_ylim()[1] * 6)
axs[0].legend(loc="upper left", fontsize=9.5, frameon=False, ncol=2)
fig.suptitle("三角形の探し方の比較(どの方式でも、画像は完全に同じ。RMSE = 0)", color=INK, fontsize=14, y=0.995)
fig.tight_layout(rect=(0, 0, 1, 0.96))
fig.savefig(OUT + "2026-10-16_m5-accel-comparison.png", dpi=110, facecolor=SURFACE); plt.close(fig)

# ---------------------------------------------------------------- 3. threads
res = json.load(open("experiments/2026-10-16_m5-threads/results.json"))
fig, ax = plt.subplots(figsize=(7.4, 5.6), facecolor=SURFACE)
ax.plot([1, 8], [1, 8], color=GRID, lw=1.6, ls=(0, (4, 3)), zorder=1)
ax.text(5.3, 5.75, "理想(スレッド数に比例)", color=MUTED, fontsize=10, ha="center", rotation=41, rotation_mode="anchor")
for scene, label, col in (("room", "Room_2Spheres(classic)", BLUE), ("cornell", "Cornell(パス + MIS)", ORANGE)):
    t = [r["threads"] for r in res[scene]]; s = [r["speedup"] for r in res[scene]]
    ax.plot(t, s, "-o", color=col, lw=2.2, ms=6, mec=SURFACE, mew=1.5, zorder=3)
    dy = 0.45 if scene == "cornell" else -0.45
    ax.text(t[-1] + 0.15, s[-1] + dy, f"{label}\n{s[-1]:.1f} 倍", color=INK, fontsize=10, va="center")
ax.axvspan(4.5, 8.5, color=GRID, alpha=0.35, lw=0, zorder=0)
ax.text(6.5, 0.55, "4 本を超えると、伸びが鈍る\n(この Mac は、性能コア 4 + 効率コア 4)", color=MUTED, fontsize=9.5, ha="center", va="bottom")
ax.set_xlim(0.7, 11.2); ax.set_ylim(0.4, 8.4); ax.set_xticks(range(1, 9))
ax.set_xlabel("スレッド数", color=MUTED, fontsize=11); ax.set_ylabel("1 スレッドに対する速度(倍)", color=MUTED, fontsize=11)
ax.set_facecolor(SURFACE); ax.grid(color=GRID, lw=0.8, zorder=0)
for sp in ("top", "right"): ax.spines[sp].set_visible(False)
for sp in ("left", "bottom"): ax.spines[sp].set_color(GRID)
ax.tick_params(colors=MUTED)
ax.set_title("スレッド数と速度(16 spp)", color=INK, fontsize=13.5, fontweight="bold", pad=10)
fig.tight_layout()
fig.savefig(OUT + "2026-10-16_m5-threads.png", dpi=110, facecolor=SURFACE)
