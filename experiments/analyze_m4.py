#!/usr/bin/env python3
"""Tabulates a run.sh of the M4 experiments: render time, rays, shadow rays, RMSE against the reference.
Usage: python3 experiments/analyze_m4.py DIR [RAYTRACE]   (writes DIR/results.txt)
Efficiency = 1 / (MSE x seconds): higher = less noise for the same time."""
import glob, json, os, subprocess, sys
d = sys.argv[1]; exe = sys.argv[2] if len(sys.argv) > 2 else "./build-release/raytrace"
rows = []
for j in sorted(glob.glob(os.path.join(d, "*.json"))):
    name = os.path.basename(j)[:-5]
    if name.startswith("ref"): continue
    info = json.load(open(j))
    group = name.split("_size")[1] if "_size" in name else ""
    ref = os.path.join(d, ("ref_size" + group if group else "reference") + ".pfm")
    rmse = float(subprocess.check_output([exe, "--rmse", os.path.join(d, name + ".pfm"), ref]).decode())
    sec = info["timing_ms"]["render_median"] / 1000
    c = info["counters_per_frame"]
    rows.append((group, name, sec, c["rays"], c.get("shadow_rays", 0), rmse, 1 / (rmse * rmse * sec)))
lines = ["%-8s %-16s %9s %12s %12s %9s %12s" % ("light", "render", "time [s]", "rays", "shadow rays", "RMSE", "efficiency")]
for g, n, sec, rays, sh, rmse, eff in rows:
    lines.append("%-8s %-16s %9.2f %12d %12d %9.4f %12.1f" % (g, n, sec, rays, sh, rmse, eff))
open(os.path.join(d, "results.txt"), "w").write("\n".join(lines) + "\n")
print("\n".join(lines))
