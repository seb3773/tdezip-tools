#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"
make clean
make -j$(nproc) all
make test
echo "=== cmix build & tests completed successfully ==="
