# Draws docs/screenshots/2026-10-16_ray-types-shadow-reflection.png: what the reflection ray and the shadow ray do at a diffuse
# point without NEE, with NEE and with MIS (needs matplotlib). Run from the repository root.
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams["font.family"] = ["Hiragino Sans", "sans-serif"]
SURFACE, INK, MUTED, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#dcdbd6"
BLUE, ORANGE, RED = "#2a78d6", "#eb6834", "#c93b2e"
LIGHT = (3.6, 5.6)                         # the light on the ceiling: x range
X0 = (3.0, 0.0)                            # the diffuse point on the floor
CAM = (-1.9, 2.4)

def room(ax):
    ax.set_xlim(-2.6, 10.8); ax.set_ylim(-1.0, 7.0); ax.set_aspect("equal"); ax.axis("off")
    ax.plot([0, 10, 10, 0], [0, 0, 6, 6], color=INK, lw=2.4, solid_capstyle="round", solid_joinstyle="round")   # floor, wall, ceiling
    ax.plot(list(LIGHT), [6, 6], color="#f2b705", lw=7, solid_capstyle="butt", zorder=3)                       # the light
    ax.text(sum(LIGHT) / 2, 6.45, "光源", ha="center", color=INK, fontsize=11)
    ax.text(10.15, 5.3, "壁", va="center", color=MUTED, fontsize=11)
    # the camera and its ray
    ax.annotate("", xy=X0, xytext=CAM, arrowprops=dict(arrowstyle="-|>", color=INK, lw=1.8))
    ax.text(CAM[0], CAM[1] + 0.35, "カメラ", ha="center", color=INK, fontsize=11)
    ax.plot(*X0, "o", color=INK, ms=8, zorder=5)
    ax.text(X0[0] - 0.2, -0.6, "拡散面の点", ha="right", color=INK, fontsize=11)

def arrow(ax, a, b, color, lw=2.0, ls="-", alpha=1.0, head=True):
    ax.annotate("", xy=b, xytext=a, arrowprops=dict(arrowstyle="-|>" if head else "-", color=color, lw=lw, ls=ls, alpha=alpha, shrinkA=0, shrinkB=0))

def hit(a, ang_deg):                                         # where a ray from a, at ang_deg from straight up, meets the room
    t = np.radians(ang_deg); d = np.array([np.sin(t), np.cos(t)])
    best = None
    for s in ((6 - a[1]) / d[1] if d[1] > 1e-9 else 1e9, (10 - a[0]) / d[0] if d[0] > 1e-9 else 1e9, (0 - a[1]) / d[1] if d[1] < -1e-9 else 1e9):
        if s > 1e-6 and (best is None or s < best): best = s
    return (a[0] + d[0] * best, a[1] + d[1] * best)

def title(ax, t, sub):
    ax.set_title(t, color=INK, fontsize=14, fontweight="bold", pad=8)
    ax.text(0.5, -0.02, sub, transform=ax.transAxes, ha="center", va="top", color=MUTED, fontsize=10.5, linespacing=1.5)

fig, axs = plt.subplots(1, 3, figsize=(17.5, 5.6), facecolor=SURFACE)

# A: no NEE -- reflection rays only; the ones that happen to reach the light carry its light
ax = axs[0]; room(ax)
for ang in (-60, -35, -10, 20, 45, 62, 75):
    arrow(ax, X0, hit(X0, ang), BLUE, lw=1.4, alpha=0.45)
lit = (4.5, 6.0)
arrow(ax, X0, lit, BLUE, lw=2.6)
ax.text(lit[0] + 0.25, 3.9, "たまたま光源に\n当たった1本だけが\n光を運ぶ", color=BLUE, fontsize=11, fontweight="bold", va="center")
title(ax, "NEE なし", "反射レイ(青)だけ。向きはランダムなので、\n光源に当たらない反射レイが大半(= ノイズの原因)")

# B: NEE -- a shadow ray to the light at every point, the reflection ray goes on
ax = axs[1]; room(ax)
L1 = (4.1, 6.0)
arrow(ax, X0, L1, ORANGE, lw=2.4, ls=(0, (4, 2)))
ax.text(2.2, 3.4, "シャドウレイ(橙)\n毎回、光源の点へ\n直接飛ばす", color=ORANGE, fontsize=11, fontweight="bold", ha="right", va="center")
X2 = hit(X0, 62)
arrow(ax, X0, X2, BLUE, lw=2.2)
ax.plot(*X2, "o", color=INK, ms=7, zorder=5)
L2 = (5.0, 6.0)
arrow(ax, X2, L2, ORANGE, lw=2.0, ls=(0, (4, 2)))
X3 = hit(X2, -150)
arrow(ax, X2, X3, BLUE, lw=1.4, alpha=0.5)
ax.text(9.5, 5.3, "次の点でも、また\nシャドウレイ", color=MUTED, fontsize=10, ha="right", va="center")
ax.text(5.9, 0.7, "反射レイ(青)は\n先へ進む(間接光)", color=BLUE, fontsize=11, fontweight="bold", ha="left", va="center")
title(ax, "NEE あり", "シャドウレイ(橙)が直接光を確実に拾う。\n反射レイ(青)は間接光を運ぶ。反射レイが光源に当たっても足さない")

# C: MIS -- both find the light; both are weighted
ax = axs[2]; room(ax)
arrow(ax, X0, (4.1, 6.0), ORANGE, lw=2.4, ls=(0, (4, 2)))
ax.text(2.3, 4.6, "シャドウレイ\n重み w₁", color=ORANGE, fontsize=11.5, fontweight="bold", ha="right", va="center")
arrow(ax, X0, (4.9, 6.0), BLUE, lw=2.4)
ax.text(5.5, 3.5, "光源に当たった\n反射レイ 重み w₂", color=BLUE, fontsize=11.5, fontweight="bold", ha="left", va="center")
for ang in (-50, -20, 38, 68):
    arrow(ax, X0, hit(X0, ang), BLUE, lw=1.3, alpha=0.4)
ax.text(6.3, 1.0, "光源に当たらない\n反射レイは\n重みなしで先へ", color=MUTED, fontsize=10, ha="left", va="center")
ax.text(7.2, -0.62, "w₁ + w₂ = 1(同じ方向に対する重み)", ha="center", color=INK, fontsize=11.5, fontweight="bold")
title(ax, "NEE + MIS", "同じ光源の光を、両方のレイで拾い、\nそれぞれ重みを掛けて足す。合計は 1 なので二重計上にならない")

fig.suptitle("拡散面の1点で、反射レイとシャドウレイがすること(部屋を横から見た断面)", color=INK, fontsize=15, y=0.995)
fig.tight_layout(rect=(0, 0.0, 1, 0.93), w_pad=1.5)
fig.savefig("docs/screenshots/2026-10-16_ray-types-shadow-reflection.png", dpi=110, facecolor=SURFACE)
