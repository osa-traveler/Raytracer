#!/bin/sh
# Russian roulette on the Cornell box: NEE, jitter, 64 spp, seed 1. Compared with the same render without roulette, at the
# usual cap of 8 bounces and at a cap of 64 (practically no truncation). Reference: roulette, cap 64, 2048 spp, seed 99.
# Run from the repository root with a Release build (RAYTRACE=... to change it); analyze.py writes results.txt.
R=${RAYTRACE:-./build-release/raytrace}
D=experiments/2026-10-15_m4-russian-roulette
B="$R Scene/Test/Cornell.glb --headless --jitter --integrator path --no-mis --progressive"
$B --spp 2048 --seed 99 --max-depth 64 --rr --out $D/reference --note "reference: NEE, roulette, cap 64, 2048 spp"
$B --spp 64 --seed 1 --max-depth 8  --out $D/off_cap8  --note "no roulette, cap 8"
$B --spp 64 --seed 1 --max-depth 64 --out $D/off_cap64 --note "no roulette, cap 64"
$B --spp 64 --seed 1 --max-depth 64 --rr --out $D/on_cap64 --note "roulette from bounce 3, cap 64"
