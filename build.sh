#!/bin/sh
# Builds Mirdain Forward against the production slicer and relaunches it.
# For a local slicer: FORWARD_SLICER_URL=http://localhost:5173/ ./build.sh
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" -DFORWARD_SLICER_URL="${FORWARD_SLICER_URL:-https://slicer.mirdainsystems.com/}"
cmake --build build --parallel
pkill -f "Mirdain Forward.app/Contents/MacOS" || true
sleep 1
open "build/Mirdain Forward.app"
