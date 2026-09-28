#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

echo "=== Building Freeze from source ==="
make clean
make -j$(nproc) all

echo "=== Running Tests ==="
make test

echo "=== Freeze build successful: ${SCRIPT_DIR}/freeze ==="
