#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${SCRIPT_DIR}/pkg_root"
VERSION="0.7-1"
ARCH="amd64"
DEB_NAME="packarc_${VERSION}_${ARCH}.deb"

echo "=== Building packARC ==="
cd "${SCRIPT_DIR}/src/source/packANY/packANYlib"
make -f Makefile_lib_Linux
make -f Makefile_lib_Linux clean
make -f Makefile_lib_Os_Linux
make -f Makefile_lib_Os_Linux clean
cp packANYlib.a packANYlib_small.a ../../packARC/

cd "${SCRIPT_DIR}/src/source/packARC"
make -f Makefile_sfx_stub.linux
make -f Makefile_sfx_stub.linux clean
gcc -o sfxstub2h sfxstub2h.c
./sfxstub2h
make -f Makefile.linux
make -f Makefile.linux clean

echo "=== Packaging DEB ==="
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/doc/packarc"
mkdir -p "${PKG_DIR}/DEBIAN"

strip -s "${SCRIPT_DIR}/src/source/packARC/packARC" -o "${PKG_DIR}/usr/bin/packARC"
chmod 755 "${PKG_DIR}/usr/bin/packARC"

# Also install into ~/.local/bin
cp -f "${PKG_DIR}/usr/bin/packARC" "${HOME}/.local/bin/packARC"

# Docs
cp "${SCRIPT_DIR}/src/README.md" "${PKG_DIR}/usr/share/doc/packarc/"
cp "${SCRIPT_DIR}/src/LICENSE" "${PKG_DIR}/usr/share/doc/packarc/copyright"

cat << 'CTRL_EOF' > "${PKG_DIR}/DEBIAN/control"
Package: packarc
Version: 0.7-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: seb3773 <99963207+seb3773@users.noreply.github.com>
Depends: libc6 (>= 2.15), libstdc++6 (>= 4.6)
Provides: packarc
Description: Multi-algorithm archiver specialized in lossless compression of JPEG, MP3, and PNM
 packARC is an archive utility created by Matthias Stirner and Se that uses
 specialized lossless re-compression engines: packJPG for JPEG files (~20% gain),
 packMP3 for MP3 audio (~16% gain), packPNM for bitmap images, and packARI
 for general data. Creates .pja (packJPG Archive) and self-extracting archives.
 Modified for non-interactive execution and custom output directory support.
 Packaged for Trinity Desktop Environment (TDE) / TdeZip companion tools.
CTRL_EOF

dpkg-deb --build --root-owner-group "${PKG_DIR}" "${SCRIPT_DIR}/${DEB_NAME}"
echo "Package created: ${SCRIPT_DIR}/${DEB_NAME}"

DEST_POOL="${SCRIPT_DIR}/../pool/main/p/packarc"
mkdir -p "${DEST_POOL}"
cp -f "${SCRIPT_DIR}/${DEB_NAME}" "${DEST_POOL}/"
echo "Copied to ${DEST_POOL}/${DEB_NAME}"

rm -rf "${PKG_DIR}"

echo "=== Updating repository metadata ==="
cd "${SCRIPT_DIR}/.."
./update_repo.sh

echo "=== packARC packaging finished successfully ==="
