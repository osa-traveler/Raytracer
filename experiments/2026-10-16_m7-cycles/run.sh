#!/bin/sh
# M7: the same scene in Blender Cycles (a separate background Blender process; an open Blender is not touched), compared with this renderer.
# Needs /Applications/Blender.app and the PBR renders of experiments/2026-10-16_m7-materials. Run from the repository root.
D=experiments/2026-10-16_m7-cycles
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python tools/cycles_render.py -- $D/cycles_1024spp 1024 > $D/run.log 2>&1
python3 experiments/analyze_m7_cycles.py experiments/2026-10-16_m7-materials $D
