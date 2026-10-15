#!/bin/sh
# MIS on the Cornell box (power-constant light of three sizes): BSDF sampling only / NEE / NEE + MIS, 8 bounces,
# jitter, 64 spp, seed 1. Reference per light size: MIS, 1024 spp, seed 99.
# Run from the repository root with a Release build (RAYTRACE=... to change it); analyze.py writes results.txt.
R=${RAYTRACE:-./build-release/raytrace}
D=experiments/2026-10-15_m4-mis
B="$R Scene/Test/Cornell.glb --headless --jitter --integrator path --max-depth 8"
for s in 0.15 0.5 1.5; do
  $B --light-size $s --spp 1024 --seed 99 --mis --out $D/ref_size$s --note "reference: MIS 1024 spp, light $s m"
  $B --light-size $s --spp 64 --seed 1 --no-nee --out $D/bsdf_size$s --note "BSDF sampling only, light $s m"
  $B --light-size $s --spp 64 --seed 1 --no-mis --out $D/nee_size$s --note "NEE, light $s m"
  $B --light-size $s --spp 64 --seed 1 --mis --out $D/mis_size$s --note "NEE + MIS, light $s m"
done
