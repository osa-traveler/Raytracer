#!/bin/sh
# Progressive timelapse: Cornell box, path tracing + NEE, 8 bounces, jitter, seed 1, passes of 1, 4, 16, 64, 256 spp.
# Writes PREFIX_spp{1,4,16,64,256}.png after each pass, and the final png / json. Run from the repository root.
D=experiments/2026-10-15_m3-progressive
${RAYTRACE:-./build-release/raytrace} Scene/Test/Cornell.glb --headless --jitter --integrator path --max-depth 8 --progressive --spp 256 --seed 1 \
  --out $D/cornell_nee --note "M3 timelapse: progressive passes 1,4,16,64,256 spp (NEE)"
# the same image in one pass, to show that passes change nothing but when you see it
${RAYTRACE:-./build-release/raytrace} Scene/Test/Cornell.glb --headless --jitter --integrator path --max-depth 8 --spp 256 --seed 1 \
  --out $D/cornell_nee_single_pass --note "single pass of 256 spp, same seed"
