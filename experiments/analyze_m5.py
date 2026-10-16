#!/usr/bin/env python3
"""Tabulates a run.sh of the M5 experiments: render time (median of the repeats), rays, BVH node visits and triangle tests
per ray, BVH build time, and the difference of the image to the reference mode (RMSE; 0 = identical).
Usage: python3 experiments/analyze_m5.py DIR [RAYTRACE]   (writes DIR/results.txt and DIR/results.json)"""
import glob, json, os, subprocess, sys
d = sys.argv[1]; exe = sys.argv[2] if len(sys.argv) > 2 else "./build-release/raytrace"
MODES = ["none", "median", "median_ord", "sah", "sah_ord", "sah_ord_any"]
rows = []
for j in sorted(glob.glob(os.path.join(d, "*.json"))):
    name = os.path.basename(j)[:-5]
    if name == "results": continue
    scene, _, mode = name.partition("_")
    if mode not in MODES: continue
    info = json.load(open(j)); c = info["counters_per_frame"]
    rows.append(dict(scene=scene, mode=mode, ms=info["timing_ms"]["render_median"], runs=len(info["timing_ms"]["render_runs"]),
                     rays=c["rays"], nodes_per_ray=c["bvh_node_visits"] / c["rays"], tris_per_ray=c["triangle_tests"] / c["rays"],
                     build_ms=info["acceleration"]["bvh_build_ms"], triangles=info["triangles"], threads=info["threads"]))
def key(r): return (r["scene"], MODES.index(r["mode"]))
rows.sort(key=key)
for r in rows:                                   # image difference to the reference (none if it exists, else median)
    ref_mode = "none" if os.path.exists(os.path.join(d, r["scene"] + "_none.pfm")) else "median"
    a = os.path.join(d, f"{r['scene']}_{r['mode']}.pfm"); b = os.path.join(d, f"{r['scene']}_{ref_mode}.pfm")
    r["rmse"] = float(subprocess.check_output([exe, "--rmse", a, b]).decode()) if os.path.exists(a) and os.path.exists(b) else None
    r["ref"] = ref_mode
    base = next((x for x in rows if x["scene"] == r["scene"] and x["mode"] == "median"), None)
    r["vs_median"] = base["ms"] / r["ms"] if base else None
lines = ["%-10s %-12s %9s %5s %11s %10s %10s %9s %9s %7s %s" % ("scene", "mode", "ms", "runs", "rays", "nodes/ray", "tris/ray", "build ms", "vs median", "RMSE", "(ref)")]
for r in rows:
    lines.append("%-10s %-12s %9.1f %5d %11d %10.1f %10.1f %9.1f %8.2fx %7s %s" % (
        r["scene"], r["mode"], r["ms"], r["runs"], r["rays"], r["nodes_per_ray"], r["tris_per_ray"], r["build_ms"],
        r["vs_median"] or 0, "-" if r["rmse"] is None else "%.6f" % r["rmse"], r["ref"]))
open(os.path.join(d, "results.txt"), "w").write("\n".join(lines) + "\n")
json.dump(rows, open(os.path.join(d, "results.json"), "w"), indent=1)
print("\n".join(lines))
