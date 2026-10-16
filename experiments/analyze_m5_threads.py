#!/usr/bin/env python3
"""Speed-up and efficiency against the number of threads. Usage: python3 experiments/analyze_m5_threads.py DIR"""
import glob, json, os, sys
d = sys.argv[1]
lines = []; out = {}
for scene in ("room", "cornell"):
    t1 = None
    lines.append(f"{scene}: threads  render ms  speed-up  efficiency")
    for t in range(1, 9):
        p = os.path.join(d, f"{scene}_t{t}.json")
        if not os.path.exists(p): continue
        ms = json.load(open(p))["timing_ms"]["render_median"]
        t1 = t1 or ms
        lines.append("   %2d %12.1f %9.2fx %9.0f%%" % (t, ms, t1 / ms, 100 * t1 / ms / t))
        out.setdefault(scene, []).append(dict(threads=t, ms=ms, speedup=t1 / ms))
    lines.append("")
open(os.path.join(d, "results.txt"), "w").write("\n".join(lines))
json.dump(out, open(os.path.join(d, "results.json"), "w"), indent=1)
print("\n".join(lines))
