#!/bin/sh
# Reproduces the normal-map convention check. Step 1 is manual (needs the user's Blender).
#  1. In Blender, run nm_blender.py (edit OUT at its top). It writes out/nm_test.glb and out/nm_test_aov.npy.
#  2. This script builds the probe, runs it on nm_test.glb and compares with Blender's normals.
set -e
cd "$(dirname "$0")"
clang++ -std=c++17 -O2 -o out/nm_probe nm_probe.cpp
./out/nm_probe out/nm_test.glb out/nm_tracer.bin
/opt/anaconda3/bin/python3 compare.py
