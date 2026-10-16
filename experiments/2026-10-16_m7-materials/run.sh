#!/bin/sh
# M7: the material rows (PBR vs the Lambert model), and light sampling on glossy surfaces. Release build (RAYTRACE=... to change).
# Scene/Test/MaterialRow.glb (tools/make_material_row.py): gold metal and red plastic, roughness 0.05 ... 1.0; glass balls IOR 1.2 / 1.5 / 2.4;
# a mirror and a grey mirror. Jitter, seed 1, 6 bounces, sky light 0.5 (the sky is not sampled: it only lights the reflections).
# Run from the repository root; then python3 experiments/analyze_m7.py DIR.
R=${RAYTRACE:-./build-release/raytrace}
D=experiments/2026-10-16_m7-materials
B="$R Scene/Test/MaterialRow.glb --headless --jitter --integrator path --max-depth 6 --env 0.5 --seed 1"
$B --bsdf pbr --spp 1024 --out $D/pbr_1024spp --note "PBR, MIS, 1024 spp" > /dev/null
$B --bsdf lambert --spp 256 --out $D/lambert_256spp --note "Lambert model (everything diffuse), 256 spp" > /dev/null
$R Scene/Test/MaterialRow.glb --headless --jitter --integrator classic --spp 16 --seed 1 --out $D/classic_16spp --note "Whitted (classic) integrator, 16 spp" > /dev/null
$B --bsdf pbr --spp 4096 --seed 99 --out $D/reference --note "reference: PBR, MIS, 4096 spp, seed 99" > /dev/null
for n in 16 64 256; do
  $B --bsdf pbr --spp $n --no-nee --out $D/bsdf_spp$n --note "PBR, BSDF sampling only, $n spp" > /dev/null
  $B --bsdf pbr --spp $n --no-mis --out $D/nee_spp$n --note "PBR, light sampling (NEE) only, $n spp" > /dev/null
  $B --bsdf pbr --spp $n --out $D/mis_spp$n --note "PBR, NEE + MIS, $n spp" > /dev/null
  echo "done $n"
done
