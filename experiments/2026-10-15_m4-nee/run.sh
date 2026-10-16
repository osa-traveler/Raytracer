#!/bin/sh
# NEE experiment on the Cornell box (8 bounces, jitter, seed 1). Run from the repository root.
# Reference: NEE, 1024 spp, seed 99 (independent of the compared images). The no-NEE images of M2 are re-rendered here
# with seeds other than the reference's.
D=experiments/2026-10-15_m4-nee
B="${RAYTRACE:-./build-release/raytrace} Scene/Test/Cornell.glb --headless --jitter --integrator path --bsdf lambert --max-depth 8 --no-mis"
$B --spp 1024 --seed 99 --out $D/reference_nee_1024 --note "reference: NEE 1024 spp"
for n in 1 4 16 64 256; do
  $B --spp $n --seed 1 --out $D/nee_spp$n --note "NEE, $n spp"
  $B --spp $n --seed 1 --no-nee --out $D/plain_spp$n --note "no NEE, $n spp"
done
