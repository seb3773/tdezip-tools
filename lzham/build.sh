#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

echo "=== Building LZHAM from source ==="
make clean
make -j$(nproc) all

echo "=== Running Tests ==="
make test

echo "=== LZHAM build successful: ${SCRIPT_DIR}/lzham ==="
