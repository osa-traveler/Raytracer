#!/bin/sh
# M5: triangle search compared across scenes (Release build; RAYTRACE=... to change). Run from the repository root.
# Modes: none (test every triangle) / median / median + nearer child first / sah / sah + nearer first / sah + nearer first + any-hit shadows.
# Scenes: Cornell (30 triangles, path tracing + MIS), Room_2Spheres (7.9k), Untitled (11.9k), the Beetle (302k triangles; 1 spp for all
# modes incl. none, 16 spp for the BVH modes). Jitter, seed 1, all hardware threads. Then python3 experiments/analyze_m5.py DIR.
R=${RAYTRACE:-./build-release/raytrace}
D=experiments/2026-10-16_m5-accel
run() {   # name scene spp repeats mode-args extra-args
  $R $2 --headless --jitter --seed 1 --spp $3 --repeat $4 $5 $6 --out $D/$1 --note "$1" > /dev/null
  echo "done $1"
}
# every flag is explicit, so the result does not depend on the defaults (the defaults are now sah + ordered + any-hit)
MODES="none:--accel_none_--no-ordered_--no-any-hit median:--accel_median_--no-ordered_--no-any-hit median_ord:--accel_median_--ordered_--no-any-hit sah:--accel_sah_--no-ordered_--no-any-hit sah_ord:--accel_sah_--ordered_--no-any-hit sah_ord_any:--accel_sah_--ordered_--any-hit"
for m in $MODES; do
  name=${m%%:*}; args=$(echo ${m#*:} | tr '_' ' ')
  run cornell_$name Scene/Test/Cornell.glb 16 3 "$args" "--integrator path --bsdf lambert --max-depth 8"
done
for m in $MODES; do
  name=${m%%:*}; args=$(echo ${m#*:} | tr '_' ' ')
  [ "$name" = sah_ord_any ] && continue            # shadow rays of the classic integrator are not any-hit (glass lets light through)
  run room_$name Scene/Test/Room_2Spheres.glb 4 3 "$args" ""
  run untitled_$name Scene/Test/Untitled.glb 4 3 "$args" ""
done
for m in $MODES; do
  name=${m%%:*}; args=$(echo ${m#*:} | tr '_' ' ')
  [ "$name" = sah_ord_any ] && continue
  reps=3; [ "$name" = none ] && reps=1
  run beetle1_$name Assets/VW_Beetle_1967.glb 1 $reps "$args" ""
  [ "$name" = none ] || run beetle16_$name Assets/VW_Beetle_1967.glb 16 3 "$args" ""
done
