#!/bin/bash
set -e
cd "$(dirname "$0")"
make clean
make -j$(nproc) all
make test
echo "Build and tests completed successfully."
