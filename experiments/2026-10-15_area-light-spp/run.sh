#!/bin/sh
# M1 step 3: soft shadows from the sampled area light, and how the error shrinks with samples per pixel.
# Run from the repository root:  sh experiments/2026-10-15_area-light-spp/run.sh      (about 3 minutes)
set -e
cd "$(dirname "$0")/../.."
OUT=experiments/2026-10-15_area-light-spp
SCENE=Scene/Test/Room_2Spheres.glb
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build >/dev/null

./build/raytrace $SCENE --headless --light area --spp 1024 --seed 777 --out $OUT/reference --note "area light, reference 1024 spp" >/dev/null
./build/raytrace $SCENE --headless --light point --out $OUT/point --note "M0 point light, hard shadow" >/dev/null
for n in 1 4 16 64 256; do
  ./build/raytrace $SCENE --headless --light area --spp $n --seed 1 --out $OUT/area_${n}spp --note "area light, $n spp" >/dev/null
done

echo "RMSE against the area-light 1024 spp reference (linear values, whole image)" | tee $OUT/rmse.txt
for f in point area_1spp area_4spp area_16spp area_64spp area_256spp; do
  printf '%-12s %s\n' $f "$(./build/raytrace --rmse $OUT/$f.pfm $OUT/reference.pfm)" | tee -a $OUT/rmse.txt
done
{ echo; echo "render time (ms, one run) and rays per frame:";
  for f in point area_1spp area_4spp area_16spp area_64spp area_256spp reference; do
    printf '%-12s %s\n' $f "$(grep -o '"render_median": [0-9.]*' $OUT/$f.json | cut -d' ' -f2) ms, $(grep -o '"rays": [0-9]*' $OUT/$f.json | cut -d' ' -f2) rays"
  done; } | tee -a $OUT/rmse.txt
