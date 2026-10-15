#!/bin/sh
# M1 experiment: area light size x samples per pixel (the roadmap's "small and large light, 1/4/16/64 samples").
# Run from the repository root:  sh experiments/2026-10-15_light-size-spp/run.sh      (about 4 minutes)
set -e
cd "$(dirname "$0")/../.."
OUT=experiments/2026-10-15_light-size-spp
SCENE=Scene/Test/Room_2Spheres.glb
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build >/dev/null

: > $OUT/rmse.txt
for size in 0.25 2 4; do                        # edge length of the square light in metres (2 = as in the glb)
  ./build/raytrace $SCENE --headless --light area --legacy-normals --light-size $size --spp 512 --seed 777 --out $OUT/size${size}_ref \
      --note "light size $size m, reference 512 spp" >/dev/null
  for n in 1 4 16 64; do
    ./build/raytrace $SCENE --headless --light area --legacy-normals --light-size $size --spp $n --seed 1 --out $OUT/size${size}_${n}spp \
        --note "light size $size m, $n spp" >/dev/null
    printf 'size %-5s spp %-3s RMSE %s\n' $size $n "$(./build/raytrace --rmse $OUT/size${size}_${n}spp.pfm $OUT/size${size}_ref.pfm)" | tee -a $OUT/rmse.txt
  done
done
