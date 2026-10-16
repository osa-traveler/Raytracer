# Draws the M7 figures for docs/RENDERING_GUIDE.md (needs matplotlib and the PNGs / results of experiments/2026-10-16_m7-materials and
# experiments/2026-10-16_m7-furnace; run from the repository root):
#   2026-10-16_m7-roughness-rows.png      the gold / plastic / glass rows, labelled
#   2026-10-16_m7-multiscatter.png        rough metal without / with the multiple-scattering compensation + the white furnace
#   2026-10-16_m7-mis-glossy.png          BSDF sampling / NEE / MIS on sharp metal, plastic, glass
#   2026-10-16_m7-model-comparison.png    Whitted / Lambert path tracing / PBR path tracing of the same scene
#   2026-10-16_m7-cycles-comparison.png   this renderer vs Blender Cycles (needs experiments/2026-10-16_m7-cycles/)
import json
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.image as mpimg

plt.rcParams["font.family"] = ["Hiragino Sans", "sans-serif"]
SURFACE, INK, MUTED, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#dcdbd6"
BLUE, ORANGE, AQUA, MAGENTA, GRAY = "#2a78d6", "#eb6834", "#1baf7a", "#e87ba4", "#8d8c86"
M = "experiments/2026-10-16_m7-materials/"
OUT = "docs/screenshots/"
img = lambda name: mpimg.imread(M + name)

# ---------------------------------------------------------------- 1. rows with labels
pbr = img("pbr_1024spp.png")
rows = [("金属(金)   roughness", (30, 146, 610, 214), [85, 162, 241, 320, 398, 476, 554], ["0.05", "0.1", "0.2", "0.35", "0.5", "0.75", "1.0"]),
        ("プラスチック(赤、非金属)   roughness", (0, 188, 640, 262), [48, 139, 230, 320, 410, 500, 591], ["0.05", "0.1", "0.2", "0.35", "0.5", "0.75", "1.0"]),
        ("ガラス球(滑らか)と鏡", (60, 236, 580, 326), [107, 213, 320, 427, 533], ["ガラス IOR 1.2", "ガラス IOR 1.5", "ガラス IOR 2.4", "鏡(F0 = 1)", "グレーの鏡(F0 = 0.5)"])]
fig, axs = plt.subplots(3, 1, figsize=(11.5, 7.6), facecolor=SURFACE, gridspec_kw=dict(height_ratios=[(r[1][3] - r[1][1]) for r in rows]))
for ax, (title, (x0, y0, x1, y1), xs, labels) in zip(axs, rows):
    ax.imshow(pbr[y0:y1, x0:x1], interpolation="bicubic", extent=(x0, x1, y1, y0)); ax.set_facecolor(SURFACE)
    ax.set_xticks(xs); ax.set_xticklabels(labels, fontsize=11, color=INK); ax.set_yticks([])
    ax.tick_params(axis="x", length=0, pad=6)
    for sp in ax.spines.values(): sp.set_visible(False)
    ax.set_title(title, loc="left", color=INK, fontsize=13, fontweight="bold", pad=6)
fig.suptitle("材質の球列(PBR、パストレーシング、MIS、1024 spp)", color=INK, fontsize=14.5, y=0.995)
fig.tight_layout(rect=(0, 0, 1, 0.97), h_pad=1.6)
fig.savefig(OUT + "2026-10-16_m7-roughness-rows.png", dpi=110, facecolor=SURFACE); plt.close(fig)

# ---------------------------------------------------------------- 2. multiple scattering + furnace
single, ms = img("pbr_1024spp_single_scatter.png"), img("pbr_1024spp.png")
fu = json.load(open("experiments/2026-10-16_m7-furnace/results.json"))
fig = plt.figure(figsize=(14.2, 5.2), facecolor=SURFACE)
gs = fig.add_gridspec(2, 2, width_ratios=[1.35, 1], height_ratios=[1, 1], hspace=0.28, wspace=0.14)
for k, (im, t) in enumerate(((single, "多重散乱の補正なし(単一散乱の GGX だけ)"), (ms, "多重散乱の補正あり(Kulla–Conty)"))):
    ax = fig.add_subplot(gs[k, 0]); ax.imshow(im[146:216, 30:610], interpolation="bicubic", extent=(30, 610, 216, 146)); ax.set_yticks([])
    xs = [85, 162, 241, 320, 398, 476, 554]; ax.set_xticks(xs); ax.set_xticklabels(["0.05", "0.1", "0.2", "0.35", "0.5", "0.75", "1.0"], fontsize=10, color=MUTED)
    ax.tick_params(axis="x", length=0)
    for sp in ax.spines.values(): sp.set_visible(False)
    ax.set_title(t + "   金属(金)の roughness", loc="left", color=INK, fontsize=12, fontweight="bold", pad=5)
ax = fig.add_subplot(gs[:, 1]); ax.set_facecolor(SURFACE)
r = fu["roughness"]
ax.axhline(1.0, color=GRID, lw=1.6, ls=(0, (4, 3)), zorder=1)
ax.plot(r, fu["metal_single"], "-o", color=ORANGE, lw=2.2, ms=6, mec=SURFACE, mew=1.4, zorder=3)
ax.plot(r, fu["metal_multiscatter"], "-o", color=BLUE, lw=2.2, ms=6, mec=SURFACE, mew=1.4, zorder=3)
ax.text(1.0, fu["metal_single"][-1] - 0.045, f"補正なし {fu['metal_single'][-1]:.2f}", color=INK, fontsize=10.5, ha="right", va="top")
ax.text(1.0, 1.045, f"補正あり {fu['metal_multiscatter'][-1]:.2f}", color=INK, fontsize=10.5, ha="right", va="bottom")
ax.text(0.12, 0.42, "プラスチック(非金属)は、\n補正の有無によらず 1.00", color=MUTED, fontsize=10, ha="left", va="center")
ax.set_ylim(0.3, 1.12); ax.set_xlim(0.05, 1.05)
ax.set_xlabel("roughness", color=MUTED, fontsize=11); ax.set_ylabel("白い炉の中の、金属球の平均の放射輝度(1 = エネルギー保存)", color=MUTED, fontsize=10.5)
ax.grid(color=GRID, lw=0.8, zorder=0)
for sp in ("top", "right"): ax.spines[sp].set_visible(False)
for sp in ("left", "bottom"): ax.spines[sp].set_color(GRID)
ax.tick_params(colors=MUTED)
ax.set_title("白い炉(白い金属球、一様な光)", color=INK, fontsize=12.5, fontweight="bold", pad=8)
fig.suptitle("粗い金属が暗くなる問題と、多重散乱の補正", color=INK, fontsize=14.5, y=0.995)
fig.savefig(OUT + "2026-10-16_m7-multiscatter.png", dpi=110, facecolor=SURFACE, bbox_inches="tight"); plt.close(fig)

# ---------------------------------------------------------------- 3. BSDF only / NEE / MIS on glossy surfaces
res = json.load(open(M + "results.json"))
crops = [("鋭い金属", (40, 150, 330, 200), 1), ("プラスチック", (10, 200, 330, 255), 2), ("ガラス", (60, 240, 330, 325), 3)]
methods = [("bsdf", "BSDF サンプリングのみ"), ("nee", "光源サンプリング(NEE)のみ"), ("mis", "NEE + MIS")]
fig, axs = plt.subplots(3, 3, figsize=(14.2, 7.4), facecolor=SURFACE, gridspec_kw=dict(height_ratios=[50, 55, 85]))
for i, (rowname, (x0, y0, x1, y1), col) in enumerate(crops):
    for j, (m, label) in enumerate(methods):
        ax = axs[i][j]; im = img(f"{m}_spp64.png")
        ax.imshow(im[y0:y1, x0:x1], interpolation="nearest"); ax.set_xticks([]); ax.set_yticks([])
        for sp in ax.spines.values(): sp.set_visible(False)
        if i == 0: ax.set_title(label, color=INK, fontsize=13, fontweight="bold", pad=7)
        ax.set_xlabel(f"この列全体の RMSE {res[f'{m}_64'][col]:.3f}", color=MUTED, fontsize=10.5, labelpad=4)
        if j == 0: ax.set_ylabel(rowname, color=INK, fontsize=11.5, fontweight="bold", labelpad=8)
fig.suptitle("つやのある面での光源サンプリングと BSDF サンプリング(同じ 64 spp、基準 = MIS 4096 spp との誤差。小さいほど良い)", color=INK, fontsize=13.5, y=0.995)
fig.tight_layout(rect=(0, 0, 1, 0.965), h_pad=1.4, w_pad=0.6)
fig.savefig(OUT + "2026-10-16_m7-mis-glossy.png", dpi=110, facecolor=SURFACE); plt.close(fig)

# ---------------------------------------------------------------- 4. the three models on the same scene
fig, axs = plt.subplots(1, 3, figsize=(15.6, 4.6), facecolor=SURFACE)
for ax, (name, t) in zip(axs, (("classic_16spp.png", "Whitted(Classic)"), ("lambert_256spp.png", "パストレーシング: Lambert(すべて拡散)"), ("pbr_1024spp.png", "パストレーシング: PBR"))):
    ax.imshow(img(name), interpolation="bicubic"); ax.axis("off"); ax.set_title(t, color=INK, fontsize=13, fontweight="bold", pad=6)
fig.suptitle("同じシーンを、3つの方式で", color=INK, fontsize=14, y=0.99)
fig.tight_layout(rect=(0, 0, 1, 0.94), w_pad=0.8)
fig.savefig(OUT + "2026-10-16_m7-model-comparison.png", dpi=110, facecolor=SURFACE)

# ---------------------------------------------------------------- 5. Blender Cycles vs this renderer
cy = json.load(open("experiments/2026-10-16_m7-cycles/results.json"))
fig = plt.figure(figsize=(14.6, 8.2), facecolor=SURFACE)
gs = fig.add_gridspec(2, 2, height_ratios=[1, 0.72], hspace=0.22, wspace=0.05)
for k, (im, t) in enumerate(((img("pbr_1024spp.png"), "このレンダラー(PBR、1024 spp)"), (mpimg.imread("experiments/2026-10-16_m7-cycles/cycles_1024spp.png"), "Blender Cycles(同じシーン、1024 spp)"))):
    ax = fig.add_subplot(gs[0, k]); ax.imshow(im, interpolation="bicubic"); ax.axis("off")
    ax.set_title(t, color=INK, fontsize=13, fontweight="bold", pad=6)
ax = fig.add_subplot(gs[1, :]); ax.set_facecolor(SURFACE)
labels = [r["name"].replace("gold", "金").replace("plastic", "プラスチック").replace("glass ior", "ガラス IOR").replace("grey mirror", "グレーの鏡").replace("mirror", "鏡").replace("r=", "r ") for r in cy]
vals = [100.0 * (sum(r["ratio"]) / 3 - 1.0) for r in cy]
colors = [{"gold": BLUE, "plastic": ORANGE, "front": AQUA}[r["group"]] for r in cy]
ax.bar(range(len(cy)), vals, color=colors, zorder=3, width=0.7)
for i, v in enumerate(vals): ax.text(i, v + (0.25 if v >= 0 else -0.25), f"{v:+.1f}", ha="center", va="bottom" if v >= 0 else "top", fontsize=8.5, color=INK)
ax.axhline(0, color=INK, lw=1.0, zorder=4)
ax.set_xticks(range(len(cy))); ax.set_xticklabels(labels, rotation=55, ha="right", fontsize=8.8, color=MUTED)
ax.set_ylabel("Cycles との差 [%]\n(球の中心部の平均、RGB の平均)", color=MUTED, fontsize=10.5); ax.set_ylim(-1.5, 8.5)
ax.grid(axis="y", color=GRID, lw=0.8, zorder=0); ax.set_axisbelow(True)
for sp in ("top", "right"): ax.spines[sp].set_visible(False)
for sp in ("left", "bottom"): ax.spines[sp].set_color(GRID)
ax.tick_params(colors=MUTED)
fig.suptitle("Blender(Cycles)との比較: 鋭い金属・鏡は 0.3% 以内、ガラス・粗い金・プラスチックは 2〜5% 明るい", color=INK, fontsize=14, y=0.995)
fig.savefig(OUT + "2026-10-16_m7-cycles-comparison.png", dpi=110, facecolor=SURFACE, bbox_inches="tight")
