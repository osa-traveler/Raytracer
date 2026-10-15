# Draws the M4 comparison figures used in docs/RENDERING_GUIDE.md (needs matplotlib and the PNGs written by
# experiments/2026-10-15_m4-mis/run.sh and experiments/2026-10-15_m4-russian-roulette/run.sh). Run from the repository root.
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.image as mpimg

plt.rcParams["font.family"] = ["Hiragino Sans", "sans-serif"]
SURFACE, INK, MUTED = "#fcfcfb", "#0b0b0b", "#52514e"
MIS = "experiments/2026-10-15_m4-mis"
RR = "experiments/2026-10-15_m4-russian-roulette"
OUT = "docs/screenshots/"

def results(d):
    rows = {}
    for line in open(d + "/results.txt").read().splitlines()[1:]:
        f = line.split()
        light, name = (f[0], f[1]) if len(f) == 7 else ("", f[0])
        rows[name] = dict(sec=float(f[-5]), rays=int(f[-4]), rmse=float(f[-2]))
    return rows

def panel(ax, path, title, sub, crop=None):
    img = mpimg.imread(path)
    if crop: x0, y0, x1, y1 = crop; img = img[y0:y1, x0:x1]
    ax.imshow(img, interpolation="nearest"); ax.axis("off")
    ax.set_title(title, color=INK, fontsize=13, fontweight="bold", pad=6)
    if sub: ax.text(0.5, -0.04, sub, transform=ax.transAxes, ha="center", va="top", color=MUTED, fontsize=10.5)

def mis_figure(size, methods, fname, headline):
    r = results(MIS)
    cols = len(methods)
    fig, axs = plt.subplots(2, cols, figsize=(4.6 * cols, 8.4), facecolor=SURFACE)
    for j, (key, label) in enumerate(methods):
        d = r[f"{key}_size{size}"]
        p = f"{MIS}/{key}_size{size}.png"
        sub = f"RMSE {d['rmse']:.4f}   時間 {d['sec']:.1f} s"
        panel(axs[0][j], p, label, sub)
        panel(axs[1][j], p, "", "", crop=(160, 230, 400, 410))      # floor and boxes, magnified
    axs[0][0].text(-0.02, 0.5, "全体", transform=axs[0][0].transAxes, rotation=90, va="center", ha="right", color=MUTED, fontsize=11)
    axs[1][0].text(-0.02, 0.5, "拡大(床と箱)", transform=axs[1][0].transAxes, rotation=90, va="center", ha="right", color=MUTED, fontsize=11)
    fig.suptitle(headline, color=INK, fontsize=14, y=0.995)
    fig.tight_layout(rect=(0.02, 0.0, 1, 0.95), h_pad=2.2)
    fig.savefig(OUT + fname, dpi=110, facecolor=SURFACE); plt.close(fig)

mis_figure("1.5", [("bsdf", "BSDF サンプリングのみ"), ("nee", "光源サンプリング(NEE)"), ("mis", "NEE + MIS")],
           "2026-10-16_m4-mis-light1.5m.png", "大きい光源(一辺 1.5 m)、64 spp、同じシード。基準画像(MIS 1024 spp)との RMSE が小さいほど、ノイズが少ない")
mis_figure("0.15", [("bsdf", "BSDF サンプリングのみ"), ("mis", "NEE + MIS(NEE のみと同じ結果)")],
           "2026-10-16_m4-mis-light0.15m.png", "小さい光源(一辺 0.15 m)、64 spp、同じシード\nBSDF サンプリングだけでは、ほとんど光源に当たらない")

r = results(RR)
fig, axs = plt.subplots(2, 3, figsize=(13.8, 8.4), facecolor=SURFACE)
for j, (key, label) in enumerate([("off_cap8", "上限 8 回・ルーレットなし"), ("off_cap64", "上限 64 回・ルーレットなし"), ("on_cap64", "上限 64 回・ルーレットあり")]):
    d = r[key]; p = f"{RR}/{key}.png"
    sub = f"時間 {d['sec']:.1f} s   レイ {d['rays'] / 1e8:.2f} 億   RMSE {d['rmse']:.4f}"
    panel(axs[0][j], p, label, sub)
    panel(axs[1][j], p, "", "", crop=(160, 230, 400, 410))
axs[0][0].text(-0.02, 0.5, "全体", transform=axs[0][0].transAxes, rotation=90, va="center", ha="right", color=MUTED, fontsize=11)
axs[1][0].text(-0.02, 0.5, "拡大(床と箱)", transform=axs[1][0].transAxes, rotation=90, va="center", ha="right", color=MUTED, fontsize=11)
fig.suptitle("Russian Roulette(64 spp、NEE): 上限 64 回で、ルーレットありは、なしと同じ画質のまま、レイ数・時間が約半分", color=INK, fontsize=13.5, y=0.995)
fig.tight_layout(rect=(0.02, 0.0, 1, 0.97), h_pad=2.2)
fig.savefig(OUT + "2026-10-16_m4-russian-roulette.png", dpi=110, facecolor=SURFACE)
