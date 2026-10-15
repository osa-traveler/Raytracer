#!/bin/sh
# Re-generates the M0 baseline: classic (Whitted) renderer, 1 spp, room + spheres.
# Run from the repository root:  sh experiments/2026-10-15_baseline/run.sh
set -e
cd "$(dirname "$0")/../.."
OUT=experiments/2026-10-15_baseline

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build >/dev/null
./build/raytrace_tests | tail -1

{
  echo "date:     $(date '+%Y-%m-%d %H:%M:%S')"
  echo "machine:  $(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -m)"
  echo "cpus:     $(sysctl -n hw.ncpu 2>/dev/null)"
  echo "os:       $(sw_vers -productName 2>/dev/null) $(sw_vers -productVersion 2>/dev/null)"
  echo "git:      $(git rev-parse --short HEAD 2>/dev/null) $(git diff --quiet 2>/dev/null || echo '(uncommitted changes)')"
} > $OUT/machine.txt

for sc in Room_2Spheres Untitled; do
  ./build/raytrace Scene/Test/$sc.glb --headless --light point --legacy-normals --repeat 5 --out $OUT/$sc \
      --note "M0 baseline: Whitted renderer, 1 spp, scene $sc.glb"
done

shasum -a 256 Scene/Test/Room_2Spheres.glb Scene/Test/Untitled.glb $OUT/*.pfm > $OUT/sha256.txt
