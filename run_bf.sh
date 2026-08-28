#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR=${DOOMBF_BUILD_DIR:-"$ROOT/build/linux"}
exec python3 "$ROOT/tools/run_pipeline.py" \
    --ibf "$BUILD_DIR/bin/ibf" \
    --frontend "$BUILD_DIR/bin/frnt" \
    --program "$BUILD_DIR/bin/doom.bpk" "$@"
