#!/usr/bin/env bash
#
# Regenerate every demo asset under media/ from the current sources.
#
# Produces:
#   media/topology.svg / .png   diagram of topology.json (Graphviz)
#   media/demo_hero.png         full walkthrough as a single still
#   media/demo.gif              scrolling terminal capture of scripts/demo.sh
#   media/demo.cast             asciinema v2 cast (replay / convert elsewhere)
#
# Requirements: a C++17 toolchain + CMake (to build the engine), Python 3 with
# Pillow, and Graphviz (dot/neato). None of asciinema/ffmpeg/agg are needed to
# produce the GIF here; the .cast is provided so those tools can be used on a
# host that has them.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build}"

if [ ! -x "$BUILD_DIR/chaos_engine" ]; then
    echo "building chaos_engine ..."
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD_DIR" --parallel --target chaos_engine >/dev/null
fi

mkdir -p media

echo "rendering topology diagram ..."
python3 scripts/render_topology.py

echo "rendering demo capture ..."
python3 scripts/render_demo.py

echo "done. assets in media/:"
ls -1 media
