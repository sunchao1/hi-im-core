#!/usr/bin/env bash
# Copyright 2026 chao.sun
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ROOT="${HIIM_ROOT:-/workspace}"
BUILD_TYPE="${HIIM_BUILD_TYPE:-Release}"
TARGET="${1:-all}"

cd "${ROOT}"

echo "[hiim-build] root=${ROOT} type=${BUILD_TYPE} target=${TARGET}"

cmake -S . -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build build -j "$(nproc)"

case "${TARGET}" in
  all)
    ctest --test-dir build --output-on-failure
    ;;
  test)
    ctest --test-dir build --output-on-failure
    ;;
  bin)
    ;;
  *)
    echo "unknown target: ${TARGET} (use all|test|bin)" >&2
    exit 1
    ;;
esac

echo "[hiim-build] done"
