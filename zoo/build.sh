#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

echo "=== Building ZOO from source ==="
make clean
make -j$(nproc) all

echo "=== Running Tests ==="
make test

echo "=== ZOO build successful: ${SCRIPT_DIR}/zoo, ${SCRIPT_DIR}/fiz ==="
