#!/bin/sh
# M5: scaling with the number of threads (Release; the machine has 4 performance + 4 efficiency cores). Default settings
# (SAH, nearer child first, any-hit shadows). Room_2Spheres: classic, area light, 16 spp. Cornell: path tracing + MIS, 16 spp.
# Run from the repository root; python3 experiments/analyze_m5_threads.py DIR summarises.
R=${RAYTRACE:-./build-release/raytrace}
D=experiments/2026-10-16_m5-threads
for t in 1 2 3 4 5 6 7 8; do
  $R Scene/Test/Room_2Spheres.glb --headless --jitter --seed 1 --spp 16 --repeat 3 --threads $t --out $D/room_t$t --note "room, $t threads" > /dev/null
  $R Scene/Test/Cornell.glb --headless --jitter --seed 1 --spp 16 --repeat 3 --integrator path --bsdf lambert --max-depth 8 --threads $t --out $D/cornell_t$t --note "cornell, $t threads" > /dev/null
  echo "done threads $t"
done
