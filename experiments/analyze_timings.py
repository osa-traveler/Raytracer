#!/usr/bin/env python3
"""Release render times of the M2 / M3 / NEE experiments next to the Debug times recorded in their run.log files.
Usage: python3 experiments/analyze_timings.py DIR"""
import json, os, re, sys
d = sys.argv[1]
def old_times(path):
    return [float(x) for x in re.findall(r"render 1/1: ([0-9.]+) ms", open(path).read())]
def new(name): return json.load(open(os.path.join(d, name + ".json")))["timing_ms"]["render_median"]
rows = []
m2 = old_times("experiments/2026-10-15_m2-cornell/run.log")        # classic, d0, d1, d2, d4, d8, spp1, 4, 16, 64, 256
rows.append(("M2 classic (Whitted), 16 spp", m2[0], new("m2_classic_16spp")))
for name, o in zip(("d0", "d1", "d2", "d4", "d8"), m2[1:6]):
    rows.append((f"M2 path, no NEE, {name[1:]} bounces, 256 spp", o, new("m2_plain_" + name)))
nee = old_times("experiments/2026-10-15_m4-nee/run.log")           # reference, then (nee, plain) for spp 1, 4, 16, 64, 256
for i, n in enumerate((1, 4, 16, 64, 256)):
    rows.append((f"NEE, 8 bounces, {n} spp", nee[1 + 2 * i], new(f"nee_nee_spp{n}")))
    rows.append((f"no NEE, 8 bounces, {n} spp", nee[2 + 2 * i], new(f"nee_plain_spp{n}")))
m3 = old_times("experiments/2026-10-15_m3-progressive/run.log")    # progressive total, single pass
rows.append(("M3 progressive 256 spp (NEE)", m3[0], new("m3_progressive_256")))
rows.append(("M3 single pass 256 spp (NEE)", m3[1], new("m3_single_256")))
lines = ["%-46s %12s %12s %9s" % ("render", "Debug [s]", "Release [s]", "speed-up")]
for name, o, n in rows:
    lines.append("%-46s %12.2f %12.2f %8.1fx" % (name, o / 1000, n / 1000, o / n))
# progressive pass times (cumulative)
log = open(os.path.join(d, "m3_progressive_256.log")).read()
passes = re.findall(r"pass: (\d+) spp\s+\(([0-9.]+) ms\)", log)
lines.append("")
lines.append("progressive passes, Release (cumulative): " + ", ".join(f"{s} spp {float(ms)/1000:.2f} s" for s, ms in passes))
open(os.path.join(d, "results.txt"), "w").write("\n".join(lines) + "\n")
json.dump([dict(name=n, debug_ms=o, release_ms=r) for n, o, r in rows], open(os.path.join(d, "results.json"), "w"), indent=1)
print("\n".join(lines))
