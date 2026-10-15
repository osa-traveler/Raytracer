#!/bin/sh
# M1 step 2 experiment: random pixel jitter (= anti-aliasing) and how the error shrinks with samples per pixel.
# Run from the repository root:  sh experiments/2026-10-15_sampler-aa/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=experiments/2026-10-15_sampler-aa
SCENE=Scene/Test/Room_2Spheres.glb
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build >/dev/null

# reference: 1024 jittered samples per pixel (not kept in Git, regenerated here)
./build/raytrace $SCENE --headless --light point --jitter --spp 1024 --seed 777 --out $OUT/reference --note "reference, 1024 spp jitter" >/dev/null
# the M0 baseline: one sample at the pixel centre
./build/raytrace $SCENE --headless --light point --out $OUT/centre_1spp --note "pixel centre, 1 spp" >/dev/null
for n in 1 4 16 64 256; do
  ./build/raytrace $SCENE --headless --light point --jitter --spp $n --seed 1 --out $OUT/jitter_${n}spp --note "jitter, $n spp" >/dev/null
done

echo "RMSE against the 1024 spp reference (linear values, whole image)"  | tee $OUT/rmse.txt
for f in centre_1spp jitter_1spp jitter_4spp jitter_16spp jitter_64spp jitter_256spp; do
  printf '%-14s %s\n' $f "$(./build/raytrace --rmse $OUT/$f.pfm $OUT/reference.pfm)" | tee -a $OUT/rmse.txt
done
{ echo; echo "render time (median of 1 run, ms) and rays per frame:";
  for f in centre_1spp jitter_1spp jitter_4spp jitter_16spp jitter_64spp jitter_256spp reference; do
    printf '%-14s %s\n' $f "$(grep -o '"render_median": [0-9.]*' $OUT/$f.json | cut -d' ' -f2) ms, $(grep -o '"rays": [0-9]*' $OUT/$f.json | cut -d' ' -f2) rays"
  done; } | tee -a $OUT/rmse.txt
