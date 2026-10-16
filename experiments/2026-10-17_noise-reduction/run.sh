#!/bin/sh
# Noise-for-accuracy switches on the beetle, view 1 (front three-quarter), 256 spp. Run from the repository root.
# Needs experiments/2026-10-17_beetle-views/out/1_front_three_quarter.glb (python3 experiments/2026-10-17_beetle-views/make_views.py 16 1).
# default.* is experiments/2026-10-17_beetle-views/1_front_three_quarter.* (no switch); reference_1024spp_default.pfm is the same view at 1024 spp.
set -e
G=experiments/2026-10-17_beetle-views/out/1_front_three_quarter.glb
O=experiments/2026-10-17_noise-reduction
run() {
    name=$1; shift
    ./build-release/raytrace "$G" --headless --integrator path --spp 256 --jitter --max-depth 8 --rr --cull-backfaces "$@" --out "$O/$name" --note "view 1, 256 spp: $*" > /dev/null
    echo "$name: $(grep -o '"render_median": [0-9.]*' "$O/$name.json" | cut -d' ' -f2) ms, $(grep -o '"noise_reduction": {[^}]*}' "$O/$name.json")"
}
run clamp3 --clamp-indirect 3
run clamp1 --clamp-indirect 1
run floor03 --glossy-floor 0.3
run glass --glass-shadows
run all3 --clamp-indirect 3 --glossy-floor 0.3 --glass-shadows
