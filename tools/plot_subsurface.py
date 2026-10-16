# Draws docs/screenshots/2026-10-17_subsurface-scattering.png: diffuse (light leaves where it came in) versus subsurface
# scattering (light wanders inside and leaves somewhere else), and what that does to a shadow edge. A schematic: the random
# walks are drawn from a fixed seed, the widths in the right-hand panel are examples, not measurements. The renderer has no
# subsurface scattering. Needs matplotlib (the Anaconda Python has it). Run from the repository root.
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Ellipse

plt.rcParams["font.family"] = ["Hiragino Sans", "sans-serif"]
SURFACE, INK, MUTED, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#dcdbd6"
BLUE, ORANGE, RED = "#2a78d6", "#eb6834", "#c93b2e"
GREEN = "#2f9e5b"
BODY = "#f4d8c8"                      # a pale, skin-like material

def arrow(ax, a, b, color, lw=2.0, alpha=1.0):
    ax.annotate("", xy=b, xytext=a, arrowprops=dict(arrowstyle="-|>", color=color, lw=lw, alpha=alpha, shrinkA=0, shrinkB=0, mutation_scale=13))

def body(ax, label):
    ax.set_xlim(-4.0, 4.0); ax.set_ylim(-3.6, 3.1); ax.set_aspect("equal"); ax.axis("off")
    ax.add_patch(Rectangle((-4.0, -3.6), 8.0, 3.6, color=BODY, lw=0))
    ax.plot([-4, 4], [0, 0], color=INK, lw=2.4)
    ax.text(-3.9, -3.3, label, color=INK, fontsize=10.5)
    ax.text(-3.9, 0.3, "空気", color=MUTED, fontsize=10.5)

def incoming(ax, p):
    start = p + np.array([-1.7, 2.3])
    arrow(ax, start, p, INK, lw=2.8)
    ax.text(start[0] - 0.1, start[1] + 0.1, "入ってくる光", ha="right", color=INK, fontsize=10.5)

fig, axs = plt.subplots(1, 3, figsize=(18.5, 6.4), facecolor=SURFACE, gridspec_kw=dict(width_ratios=[1, 1, 0.95]))

# ---------------------------------------------------------------- A: diffuse
ax = axs[0]; body(ax, "不透明な材質(紙、塗装、石こう)")
P = np.array([0.0, 0.0])
incoming(ax, P)
for a in np.linspace(-65, 65, 7):
    t = np.radians(a)
    arrow(ax, P, P + 1.7 * np.array([np.sin(t), np.cos(t)]), ORANGE, lw=1.7, alpha=0.95)
ax.plot(*P, "o", color=INK, ms=8, zorder=5)
ax.text(0.0, -0.45, "入った点 = 出る点", ha="center", color=INK, fontsize=11, fontweight="bold")
ax.text(0.0, -1.5, "光は、ごく浅いところで散って、\nすぐ同じ点から出てくる。\n計算は、その 1 点だけで済む", ha="center", color="#9a5a00", fontsize=10.5, linespacing=1.4)
ax.set_title("拡散反射(Lambert): 入った点から出る", color=INK, fontsize=14, fontweight="bold", pad=8)

# ---------------------------------------------------------------- B: subsurface scattering (random walks)
ax = axs[1]; body(ax, "半透明な材質(皮膚、蝋、大理石、牛乳)")
rng = np.random.default_rng(7)
P = np.array([-0.9, 0.0])
ax.add_patch(Ellipse((P[0] + 0.7, 0), 5.6, 3.2, color="#f6a98a", alpha=0.30, lw=0, zorder=1))   # where the light comes out
incoming(ax, P)
exits = []
walks = []
for k in range(60):                                    # random walks: straight down first, then a new random direction at every scatter
    pos = P.copy(); d = np.array([0.0, -1.0]); pts = [pos.copy()]; escaped = False
    for step in range(200):
        pos = pos + d * rng.exponential(0.55)
        if pos[1] >= 0:
            pos[1] = 0.0; pts.append(pos.copy()); escaped = True; break
        pts.append(pos.copy())
        if rng.random() < 0.03 or pos[1] < -3.3: break                                  # absorbed (or too deep to draw)
        a_ = rng.uniform(0, 2 * np.pi); d = np.array([np.cos(a_), np.sin(a_)])
    if len(pts) > 2:
        walks.append((np.array(pts), escaped))
shown_e = shown_a = 0
for pts, escaped in walks:
    if escaped and shown_e < 12 and abs(pts[-1][0] - P[0]) > 0.3 and pts[:, 0].min() > -3.8 and pts[:, 0].max() < 3.8 and pts[:, 1].min() > -3.0:
        shown_e += 1; exits.append(pts[-1][0])
        ax.plot(pts[:, 0], pts[:, 1], color=ORANGE, lw=1.6, alpha=0.95, zorder=3)
    elif not escaped and shown_a < 4 and pts[:, 1].min() > -3.3 and abs(pts[:, 0]).max() < 3.8:
        shown_a += 1
        ax.plot(pts[:, 0], pts[:, 1], color=MUTED, lw=1.0, alpha=0.5, zorder=2)
for x in exits:
    arrow(ax, np.array([x, 0.0]), np.array([x + 0.2, 0.7]), ORANGE, lw=1.6)
ax.plot(*P, "o", color=INK, ms=8, zorder=6)
ax.text(P[0] - 0.15, -0.35, "入った点", ha="right", color=INK, fontsize=11, fontweight="bold", bbox=dict(facecolor=BODY, edgecolor="none", pad=1.5), zorder=7)
ax.text(2.0, 0.9, "別の場所から出る", ha="center", color="#9a5a00", fontsize=11, fontweight="bold")
ax.text(0.0, -2.55, "光は中で何度も散り、一部は吸収され、\n残りが入った点の周りの広い範囲から出てくる", ha="center", va="center", color="#7a4500", fontsize=10.5, linespacing=1.4, bbox=dict(facecolor=BODY, edgecolor="none", alpha=0.92, pad=3), zorder=8)
ax.set_title("表面下散乱(SSS): 別の場所から出る", color=INK, fontsize=14, fontweight="bold", pad=8)

# ---------------------------------------------------------------- C: a shadow edge
ax = axs[2]
x = np.linspace(-3, 3, 600)
from math import erf
def blur(w): return 0.5 * (1 + np.array([erf(v / (w * np.sqrt(2))) for v in x]))
ax.plot(x, (x > 0).astype(float), color=INK, lw=2.8, label="拡散反射(Lambert): くっきり")
ax.plot(x, blur(0.9), color=RED, lw=2.2, label="SSS の赤(遠くまで広がる)")
ax.plot(x, blur(0.5), color=GREEN, lw=2.2, label="SSS の緑")
ax.plot(x, blur(0.3), color=BLUE, lw=2.2, label="SSS の青(あまり広がらない)")
ax.axvline(0, color=GRID, lw=1.2, ls=(0, (4, 3)))
ax.text(-2.9, 1.28, "影", color=MUTED, fontsize=11); ax.text(2.4, 1.28, "光が当たる", color=MUTED, fontsize=11)
ax.set_xlim(-3, 3); ax.set_ylim(-0.02, 1.4)
ax.set_xlabel("影の境目からの位置", color=INK, fontsize=11); ax.set_ylabel("見える明るさ(例)", color=INK, fontsize=11)
ax.set_facecolor(SURFACE)
for s in ("top", "right"): ax.spines[s].set_visible(False)
for s in ("left", "bottom"): ax.spines[s].set_color(MUTED)
ax.tick_params(colors=MUTED); ax.grid(color=GRID, lw=0.8)
ax.legend(loc="upper left", bbox_to_anchor=(0.0, 0.9), frameon=False, fontsize=10.5)
ax.annotate("影の側に、光がにじみ出る。\n色によって広がりが違うので、\n境目が赤っぽく見える(皮膚)", xy=(-0.9, 0.12), xytext=(-2.9, 0.42), color=INK, fontsize=10.5,
            arrowprops=dict(arrowstyle="-", color=MUTED, lw=1.0), linespacing=1.35)
ax.set_title("影の境目の見え方", color=INK, fontsize=14, fontweight="bold", pad=8)

fig.suptitle("表面下散乱(Subsurface)― 光が材質の中で散って、入った場所とは別の場所から出てくる(模式図)", color=INK, fontsize=15.5, y=0.995)
fig.text(0.5, 0.012, "左と中央: 断面の模式図(中央の光の経路は、乱数で描いた例)。右: 広がりの幅は例で、測った値ではない。このレンダラーは、まだ SSS に未対応。",
         ha="center", color=MUTED, fontsize=10)
fig.tight_layout(rect=(0, 0.04, 1, 0.94), w_pad=2.0)
fig.savefig("docs/screenshots/2026-10-17_subsurface-scattering.png", dpi=110, facecolor=SURFACE)
print("wrote docs/screenshots/2026-10-17_subsurface-scattering.png")
