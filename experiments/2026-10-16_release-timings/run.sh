#!/bin/sh
# The render times of the M2 / M3 / NEE experiments (2026-10-15) were measured with a Debug build. This re-measures the same
# settings with a Release build and the default triangle search (SAH). One run each (+-10% noise); the images are the same as before.
# Cornell box, jitter, seed 1, all hardware threads. Run from the repository root; python3 experiments/analyze_timings.py DIR summarises.
R=${RAYTRACE:-./build-release/raytrace}
D=experiments/2026-10-16_release-timings
C="$R Scene/Test/Cornell.glb --headless --jitter --seed 1 --integrator path"
$R Scene/Test/Cornell.glb --headless --jitter --seed 1 --spp 16 --out $D/m2_classic_16spp > /dev/null
for d in 0 1 2 4 8; do $C --spp 256 --max-depth $d --no-nee --no-mis --out $D/m2_plain_d$d > /dev/null; echo "done m2 d$d"; done
for n in 1 4 16 64 256; do
  $C --spp $n --max-depth 8 --no-nee --no-mis --out $D/nee_plain_spp$n > /dev/null
  $C --spp $n --max-depth 8 --no-mis --out $D/nee_nee_spp$n > /dev/null
  echo "done nee spp$n"
done
$C --spp 256 --max-depth 8 --no-mis --progressive --out $D/m3_progressive_256 > $D/m3_progressive_256.log
$C --spp 256 --max-depth 8 --no-mis --out $D/m3_single_256 > /dev/null
echo "done m3"
