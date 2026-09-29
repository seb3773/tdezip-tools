#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Building packANY libraries ==="
cd "${SCRIPT_DIR}/src/source/packANY/packANYlib"
make -f Makefile_lib_Linux
make -f Makefile_lib_Linux clean
make -f Makefile_lib_Os_Linux
make -f Makefile_lib_Os_Linux clean
cp packANYlib.a packANYlib_small.a ../../packARC/

echo "=== Building packARC binary and SFX stub ==="
cd "${SCRIPT_DIR}/src/source/packARC"
make -f Makefile_sfx_stub.linux
make -f Makefile_sfx_stub.linux clean
gcc -o sfxstub2h sfxstub2h.c
./sfxstub2h
make -f Makefile.linux
make -f Makefile.linux clean

cp -f packARC "${SCRIPT_DIR}/packARC"
echo "=== packARC build finished: ${SCRIPT_DIR}/packARC ==="
