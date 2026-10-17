#!/bin/sh
# Is the noise inside the cabin (seats, ceiling, seen through the windows) caused by the materials, or by the light reaching it only through glass?
# The user's preset my-settings_best (1024 spp, clamp 10, glossy floor 0.89, glass shadows OFF) with only --glass-shadows added, views 1 and 4.
# Compare with ../best/*_my-settings_best.pfm (the same, glass shadows off). Run from the repository root.
set -e
for v in 1_front_three_quarter 4_high_from_front; do
    ./build-release/raytrace experiments/2026-10-17_beetle-views/out/$v.glb --headless --preset my-settings_best --glass-shadows \
        --out experiments/2026-10-17_beetle-views/interior/${v}_best_glass --note "beetle view $v, preset my-settings_best + --glass-shadows" > /dev/null
    echo "$v: $(grep -o '"render_median": [0-9.]*' experiments/2026-10-17_beetle-views/interior/${v}_best_glass.json | cut -d' ' -f2) ms"
done
