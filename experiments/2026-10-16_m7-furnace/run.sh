#!/bin/sh
# Builds and runs the white furnace (see furnace.cpp); writes results.json. Run from the repository root.
D=experiments/2026-10-16_m7-furnace
clang++ -std=c++17 -O2 -I. $D/furnace.cpp -o /tmp/furnace -pthread && /tmp/furnace > $D/results.json && cat $D/results.json
