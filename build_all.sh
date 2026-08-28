#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR=${DOOMBF_BUILD_DIR:-"$ROOT/build/linux"}

if [ "${1:-}" = "clean" ]; then
    rm -rf -- "$BUILD_DIR"
    shift
fi

if [ -n "${RISCV_GCC:-}" ]; then
    set -- "-DRISCV_GCC_EXECUTABLE:FILEPATH=$RISCV_GCC" "$@"
fi

cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDOOMBF_BUILD_NATIVE=ON \
    -DDOOMBF_BUILD_FRONTEND=ON \
    -DDOOMBF_BUILD_IBF=ON \
    -DDOOMBF_BUILD_BF=ON \
    "$@"
cmake --build "$BUILD_DIR" --parallel --target doombf_all
