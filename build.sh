#!/bin/sh
# Builds Mirdain Forward and relaunches it against production: the slicer at
# slicer.mirdainsystems.com and Gwaith at gwaith.mirdainsystems.com.
# ./build.sh dev uses local ones instead: the slicer at http://localhost:5173/ and Gwaith at
# http://localhost:8001.
# For others: FORWARD_SLICER_URL=http://localhost:5181/ FORWARD_GWAITH_URL=http://localhost:8002 ./build.sh
set -e
cd "$(dirname "$0")"
case "${1:-}" in
    dev) slicer=http://localhost:5173/ gwaith=http://localhost:8001 ;;
    "") slicer="${FORWARD_SLICER_URL:-https://slicer.mirdainsystems.com/}" gwaith="${FORWARD_GWAITH_URL:-https://gwaith.mirdainsystems.com}" ;;
    *) echo "Usage: $0 [dev]" >&2; exit 1 ;;
esac
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" -DFORWARD_SLICER_URL="$slicer" -DFORWARD_GWAITH_URL="$gwaith"
cmake --build build --parallel
pkill -f "Mirdain Forward.app/Contents/MacOS" || true
sleep 1
open "build/Mirdain Forward.app"
