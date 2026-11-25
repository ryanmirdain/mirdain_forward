#!/bin/sh
# Builds Mirdain Forward and relaunches it. The slicer window loads the production slicer;
# ./build.sh dev loads a local one at http://localhost:5173/ instead.
# For another slicer: FORWARD_SLICER_URL=http://localhost:5181/ ./build.sh
set -e
cd "$(dirname "$0")"
case "${1:-}" in
    dev) slicer=http://localhost:5173/ ;;
    "") slicer="${FORWARD_SLICER_URL:-https://slicer.mirdainsystems.com/}" ;;
    *) echo "Usage: $0 [dev]" >&2; exit 1 ;;
esac
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" -DFORWARD_SLICER_URL="$slicer"
cmake --build build --parallel
pkill -f "Mirdain Forward.app/Contents/MacOS" || true
sleep 1
open "build/Mirdain Forward.app"
