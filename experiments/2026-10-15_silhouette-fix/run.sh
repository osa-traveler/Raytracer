#!/bin/sh
# Effect of the silhouette fix (decide front/back by the triangle's geometric normal, clamp a back-facing shading normal)
# compared with the M0 behaviour (--legacy-normals). Run from the repository root:
#   sh experiments/2026-10-15_silhouette-fix/run.sh      (about 40 seconds)
set -e
cd "$(dirname "$0")/../.."
OUT=experiments/2026-10-15_silhouette-fix
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build >/dev/null

# 1 spp, pixel centre, point light: the M0 baseline settings
for sc in Room_2Spheres Untitled; do
  ./build/raytrace Scene/Test/$sc.glb --headless --light point --legacy-normals --out $OUT/${sc}_1spp_legacy >/dev/null
  ./build/raytrace Scene/Test/$sc.glb --headless --light point --out $OUT/${sc}_1spp_fixed >/dev/null
done
# 64 spp with jitter and the area light (what the latest screenshot shows)
./build/raytrace Scene/Test/Untitled.glb --headless --light area --jitter --spp 64 --seed 1 --legacy-normals --out $OUT/Untitled_64spp_legacy >/dev/null
./build/raytrace Scene/Test/Untitled.glb --headless --light area --jitter --spp 64 --seed 1 --out $OUT/Untitled_64spp_fixed >/dev/null
