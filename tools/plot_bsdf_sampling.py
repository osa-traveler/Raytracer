# Draws docs/screenshots/2026-10-15_bsdf-sampling-directions.png (needs matplotlib). Run from the repository root.
import numpy as np, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager
plt.rcParams["font.family"] = ["Hiragino Sans", "Hiragino Kaku Gothic Pro", "sans-serif"]

SURFACE, INK, MUTED, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#dcdbd6"
BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"

inc_deg = 45                                   # light arrives from 45 deg to the left of the normal
inc = np.radians(inc_deg)
mirror = inc                                   # mirror direction: 45 deg to the right (the ray is drawn arriving from the left)
# angle t is measured from the normal (0 = straight up), -90..90 in the plane of incidence
t = np.radians(np.linspace(-90, 90, 721))

def to_xy(theta, r):                           # theta from the normal, x right, y up
    return r * np.sin(theta), r * np.cos(theta)

fig, axs = plt.subplots(1, 3, figsize=(13.5, 5.0), facecolor=SURFACE)
titles = ["拡散面(コサイン重み付き)", "鏡面反射(鏡)", "つやのある面"]
subs = ["紙・壁など。法線に近いほど選ばれやすい", "反射方向の1本だけ", "鏡面反射の方向のまわりに集中"]
cols = [BLUE, ORANGE, AQUA]

def setup(ax):
    ax.set_facecolor(SURFACE); ax.set_aspect("equal")
    ax.set_xlim(-1.35, 1.35); ax.set_ylim(-0.12, 1.45); ax.axis("off")
    ax.plot([-1.3, 1.3], [0, 0], color=INK, lw=2, solid_capstyle="round")          # the surface
    for x in np.linspace(-1.25, 1.2, 12):                                        # hatching under the surface
        ax.plot([x, x - 0.08], [0, -0.09], color=MUTED, lw=0.9)
    for r in (0.5, 1.0):                                                          # recessive grid
        a = np.linspace(-np.pi / 2, np.pi / 2, 100)
        ax.plot(r * np.sin(a), r * np.cos(a), color=GRID, lw=0.8, ls=(0, (3, 3)))
    ax.annotate("", xy=(0, 1.25), xytext=(0, 0), arrowprops=dict(arrowstyle="-|>", color=MUTED, lw=1.2))
    ax.text(0.04, 1.28, "法線", color=MUTED, fontsize=11, va="bottom")

def incoming(ax):                                                                 # the arriving light ray
    x0, y0 = to_xy(inc, 1.3)
    ax.annotate("", xy=(0, 0), xytext=(-x0 * -1 * -1, y0), arrowprops=dict(arrowstyle="-|>", color=INK, lw=1.6))
    ax.text(-x0 * -1 * -1 - 0.05, y0 + 0.03, "入射光", color=INK, fontsize=11, ha="right")

# 1 diffuse: pdf = cos(theta)/pi (independent of the incoming direction)
ax = axs[0]; setup(ax)
r = np.cos(t); x, y = to_xy(t, r)
ax.fill(np.r_[x, 0], np.r_[y, 0], color=BLUE, alpha=0.22, lw=0); ax.plot(x, y, color=BLUE, lw=2.2)
ax.text(0, 0.62, "確率\n∝ cos θ", color=BLUE, fontsize=12, ha="center", fontweight="bold")
peak = [f"最大 {1/np.pi:.2f}(法線方向)", "どの入射方向でも同じ形"]

# 2 mirror: a single direction
ax = axs[1]; setup(ax)
mx, my = to_xy(mirror, 1.0)
ax.annotate("", xy=(mx, my), xytext=(0, 0), arrowprops=dict(arrowstyle="-|>", color=ORANGE, lw=3.2))
ax.text(mx + 0.06, my - 0.02, "確率 100%", color=ORANGE, fontsize=12, fontweight="bold")
peak.append(None); peak[1:] = ["反射方向だけ(確率は1点に集中)", None]
peak = [f"最大 {1/np.pi:.2f}(法線方向)", "反射方向だけ(確率は1点に集中)", None]

# 3 glossy: Phong-like lobe around the mirror direction, pdf = (n+1)/(2 pi) cos^n(alpha) (3D); slice here
n = 40
alpha = t - mirror
r = np.clip(np.cos(alpha), 0, None) ** n
r = np.where(np.cos(t) > 0, r, 0)
ax = axs[2]; setup(ax)
x, y = to_xy(t, r)
ax.fill(np.r_[x, 0], np.r_[y, 0], color=AQUA, alpha=0.25, lw=0); ax.plot(x, y, color=AQUA, lw=2.2)
mx, my = to_xy(mirror, 1.0)
ax.plot([0, mx], [0, my], color=AQUA, lw=1, ls=(0, (2, 2)))
ax.text(mx + 0.06, my - 0.02, "鏡面反射の方向", color=MUTED, fontsize=10)
peak[2] = f"最大 {(n+1)/(2*np.pi):.1f}(例: n = {n})"
peak[1] = "反射方向の1本だけ"

for i, ax in enumerate(axs):
    ax.set_title(titles[i], color=INK, fontsize=15, fontweight="bold", pad=8)
    ax.text(0, -0.26, subs[i], color=MUTED, fontsize=11, ha="center")
    ax.text(0, -0.40, "ピークの確率密度: " + (["0.32 (= 1/π)", "∞(1方向に集中)", f"{(n+1)/(2*np.pi):.1f}(n = {n})"][i]),
            color=INK, fontsize=11, ha="center")
    if i != 0:
        pass
    # incoming ray, drawn the same in every panel
    sx, sy = to_xy(inc, 1.3)
    ax.annotate("", xy=(0, 0), xytext=(-sx, sy), arrowprops=dict(arrowstyle="-|>", color=MUTED, lw=1.6, ls="--"))
    ax.text(-sx - 0.04, sy + 0.02, "入射光", color=MUTED, fontsize=10, ha="right")
    ax.set_ylim(-0.5, 1.45)

fig.suptitle("反射する方向の選ばれやすさ(面を真横から見た断面。線の長さ = 選ばれやすさ)", color=INK, fontsize=14, y=0.99)
fig.text(0.5, 0.01, "どれも「法線に近い/反射方向に近い方向ほど、よく選ぶ」。選ぶ確率(PDF)を、寄与の補正に使う。  ※拡散面は入射方向によらず同じ形。断面なので、3次元では、この形を法線(または反射方向)のまわりに回転させたもの。",
         color=MUTED, fontsize=9, ha="center")
fig.tight_layout(rect=(0, 0.04, 1, 0.96))
fig.savefig("docs/screenshots/2026-10-15_bsdf-sampling-directions.png", dpi=150, facecolor=SURFACE)
