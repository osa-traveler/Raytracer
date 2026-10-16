# Draws docs/screenshots/2026-10-17_clearcoat-reflection.png: how light is reflected by a car paint without and with a clear coat
# (cross-sections of the layers, and the shape of the reflection seen from the camera). A schematic: the angles follow Snell's
# law, the numbers in the right-hand panel (widths, heights) are examples, not measurements. The renderer has no clear coat yet.
# Needs matplotlib (the Anaconda Python has it). Run from the repository root.
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

plt.rcParams["font.family"] = ["Hiragino Sans", "sans-serif"]
SURFACE, INK, MUTED, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#dcdbd6"
BLUE, ORANGE, RED = "#2a78d6", "#eb6834", "#c93b2e"
PAINT, COAT = "#f2b705", "#bfdcf7"
N = 1.5                                       # refractive index of the clear coat
TH = np.radians(50.0)                         # angle of the incoming light from the surface normal
TH_IN = np.arcsin(np.sin(TH) / N)             # the same ray inside the coat (Snell's law)

def arrow(ax, a, b, color, lw=2.2, alpha=1.0, ls="-", head=True):
    ax.annotate("", xy=b, xytext=a, arrowprops=dict(arrowstyle="-|>" if head else "-", color=color, lw=lw, alpha=alpha, ls=ls,
                                                    shrinkA=0, shrinkB=0, mutation_scale=14))

def dirv(angle, up=True):                      # unit vector, `angle` from the normal, towards +x; up or down
    return np.array([np.sin(angle), np.cos(angle) if up else -np.cos(angle)])

def frame(ax, coat):
    ax.set_xlim(-3.2, 3.6); ax.set_ylim(-3.1, 3.0); ax.set_aspect("equal"); ax.axis("off")
    top = 0.0
    if coat:
        ax.add_patch(Rectangle((-3.2, -1.0), 6.8, 1.0, color=COAT, alpha=0.75, lw=0))
        ax.text(-3.1, -0.5, "クリアコート\n(透明な層)", color=INK, fontsize=10.5, va="center", linespacing=1.3)
        ax.add_patch(Rectangle((-3.2, -3.1), 6.8, 2.1, color=PAINT, alpha=0.55, lw=0))
        ax.text(-3.1, -2.1, "塗装(黄色い顔料)", color=INK, fontsize=10.5, va="center")
        ax.plot([-3.2, 3.6], [-1, -1], color=MUTED, lw=1.2)
    else:
        ax.add_patch(Rectangle((-3.2, -3.1), 6.8, 3.1, color=PAINT, alpha=0.55, lw=0))
        ax.text(-3.1, -1.5, "塗装(黄色い顔料)", color=INK, fontsize=10.5, va="center")
    ax.plot([-3.2, 3.6], [top, top], color=INK, lw=2.4)
    ax.text(-3.1, 0.35, "空気", color=MUTED, fontsize=10.5)
    ax.plot([0, 0], [0, 1.0], color=GRID, lw=1.2, ls=(0, (4, 3)), zorder=1)          # the normal at the hit point
    ax.text(0.06, 1.05, "法線", color=MUTED, fontsize=9.5)

def incoming(ax, p):
    start = p + np.array([-np.sin(TH), np.cos(TH)]) * 2.6
    arrow(ax, start, p, INK, lw=2.6)
    ax.text(start[0] - 0.15, start[1] + 0.1, "入ってくる光", ha="right", color=INK, fontsize=10.5)

def scatter(ax, p, top_y, color, n=5, spread=62, reach=1.45, alpha=0.9, refract=False):
    """Thin arrows leaving the point p in all upward directions (diffuse); inside the coat they are bent towards the normal."""
    for a in np.linspace(-spread, spread, n):
        ang = np.radians(a)
        d = dirv(ang)
        arrow(ax, p, p + d * reach, color, lw=1.4, alpha=alpha)

# ---------------------------------------------------------------- panel A: no clear coat
fig, axs = plt.subplots(1, 3, figsize=(18.5, 6.4), facecolor=SURFACE, gridspec_kw=dict(width_ratios=[1, 1, 0.9]))
ax = axs[0]; frame(ax, coat=False)
P = np.array([0.0, 0.0])
incoming(ax, P)
arrow(ax, P, P + dirv(TH) * 2.5, BLUE, lw=3.0)                                         # mirror reflection at the paint surface
ax.text(2.0, 2.15, "表面の反射\n(約 4%・無色)\n粗さでぼける", color=BLUE, fontsize=10.5, va="bottom", linespacing=1.35)
Q = P + dirv(TH_IN, up=False) * 0.35
arrow(ax, P, np.array([0.18, -0.55]), ORANGE, lw=2.0)
scatter(ax, np.array([0.18, -0.55]), 0, ORANGE, n=5, spread=58, reach=1.15, alpha=0.9)
ax.text(1.35, -0.85, "染み込んだ光は顔料で\nあちこちに散り、\n黄色く出てくる(拡散)", color="#9a5a00", fontsize=10.5, linespacing=1.35)
ax.set_title("コートなし: 反射の層は 1 つ", color=INK, fontsize=14, fontweight="bold", pad=8)

# ---------------------------------------------------------------- panel B: with a clear coat
ax = axs[1]; frame(ax, coat=True)
P1 = np.array([0.0, 0.0])
incoming(ax, P1)
arrow(ax, P1, P1 + dirv(TH) * 2.5, BLUE, lw=3.4)                                       # 1: the coat surface (sharp)
ax.text(2.0, 2.15, "① コート表面の反射\n(約 4%・無色)\n鋭い = 映り込みがくっきり", color=BLUE, fontsize=10.5, va="bottom", linespacing=1.35)
P2 = P1 + np.array([np.tan(TH_IN) * 1.0, -1.0])                                         # the light goes through the coat (it bends)
arrow(ax, P1, P2, INK, lw=2.2, alpha=0.85)
ax.text(-0.5, -0.58, "屈折", color=MUTED, fontsize=9.5)
P3 = P2 + np.array([np.tan(TH_IN) * 1.0, 1.0])                                          # 2: reflection at the paint (below the coat)
arrow(ax, P2, P3, ORANGE, lw=2.2)
arrow(ax, P3, P3 + dirv(TH) * 1.55, ORANGE, lw=2.2)                                     # leaves the coat bent away from the normal
ax.text(2.55, 1.45, "② 塗装面の反射\n(色つき・ぼける)\nコートの下から出てくる", color="#9a5a00", fontsize=10.5, va="center", linespacing=1.35)
for dx, a in ((0.55, -33), (0.95, -10), (1.3, 22)):                                    # (inside angles stay below the critical angle, 41.8 deg)
    # 3: diffuse light coming out through the coat
    s = np.array([P2[0] - 0.35 + dx * 0.4, -1.0 - 0.02])
    ang_in = np.radians(a)
    top = s + np.array([np.tan(ang_in), 1.0])
    arrow(ax, s, top, "#d4a000", lw=1.3, alpha=0.9)
    ang_out = np.arcsin(N * np.sin(ang_in))
    arrow(ax, top, top + dirv(ang_out) * 0.9, "#d4a000", lw=1.3, alpha=0.9)
ax.text(-3.1, -2.75, "③ 拡散した光も、コートを通って出てくる(黄色)", color="#9a5a00", fontsize=10.5)
ax.set_title("コートあり: 反射の層が 2 つ(+ 拡散)", color=INK, fontsize=14, fontweight="bold", pad=8)

# ---------------------------------------------------------------- panel C: what the camera sees around the mirror direction
ax = axs[2]
ang = np.linspace(-45, 45, 600)
def lobe(w, h): return h * np.exp(-0.5 * (ang / w) ** 2)
base_only = lobe(13, 0.28)
coat = lobe(2.2, 1.0)
base_under = lobe(13, 0.24)                    # the base lobe is a little weaker: the coat takes some of the light
ax.fill_between(ang, 0, base_only, color=ORANGE, alpha=0.30, lw=0)
ax.plot(ang, base_only, color=ORANGE, lw=2.6, label="コートなし(1 つのなだらかな山)")
ax.plot(ang, coat + base_under, color=INK, lw=2.8, label="コートあり(合計)")
ax.plot(ang, coat, color=BLUE, lw=1.8, ls="--", label="  ① コート表面(鋭い山)")
ax.plot(ang, base_under, color=ORANGE, lw=1.8, ls="--", label="  ② 塗装面(なだらかな山)")
ax.set_xlim(-45, 45); ax.set_ylim(0, 1.75)
ax.set_xlabel("鏡の反射方向からのずれ(度)", color=INK, fontsize=11)
ax.set_ylabel("反射の強さ(例)", color=INK, fontsize=11)
ax.set_facecolor(SURFACE)
for s in ("top", "right"): ax.spines[s].set_visible(False)
for s in ("left", "bottom"): ax.spines[s].set_color(MUTED)
ax.tick_params(colors=MUTED)
ax.grid(color=GRID, lw=0.8)
ax.legend(loc="upper left", frameon=False, fontsize=10.5)
ax.annotate("映り込みの輪郭はくっきり、\nその下に塗装の色のつやが広がる\n= 車の塗装の深み", xy=(7, 0.35), xytext=(13, 0.66), color=INK, fontsize=10.5,
            arrowprops=dict(arrowstyle="-", color=MUTED, lw=1.0), linespacing=1.35)
ax.set_title("カメラから見える反射の形", color=INK, fontsize=14, fontweight="bold", pad=8)

fig.suptitle("クリアコート(Coat)の光の反射 ― 塗装の上に透明な層があると、反射が 2 回起きる(模式図)", color=INK, fontsize=15.5, y=0.995)
fig.text(0.5, 0.012, "左と中央: 層の断面(角度は屈折の法則どおり、コートの屈折率 1.5)。右: 山の幅と高さは例で、測った値ではない。このレンダラーは、まだ Coat に未対応。",
         ha="center", color=MUTED, fontsize=10)
fig.tight_layout(rect=(0, 0.04, 1, 0.94), w_pad=2.0)
fig.savefig("docs/screenshots/2026-10-17_clearcoat-reflection.png", dpi=110, facecolor=SURFACE)
print("wrote docs/screenshots/2026-10-17_clearcoat-reflection.png")
