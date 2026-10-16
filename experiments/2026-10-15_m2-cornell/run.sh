#!/bin/sh
# Cornell box: bounce count sweep (path) + classic reference + spp convergence. Run from the repository root.
D=experiments/2026-10-15_m2-cornell
B="${RAYTRACE:-./build-release/raytrace} Scene/Test/Cornell.glb --headless --jitter --seed 1"
$B --integrator classic --spp 16 --out $D/classic --note "Whitted reference: direct light only + constant ambient"
for d in 0 1 2 4 8; do
  $B --integrator path --bsdf lambert --spp 256 --max-depth $d --out $D/path_d$d --note "path tracing, $d bounces, 256 spp"
done
for n in 1 4 16 64 256; do
  $B --integrator path --bsdf lambert --spp $n --max-depth 8 --out $D/path_spp$n --note "path tracing, 8 bounces, $n spp"
done
