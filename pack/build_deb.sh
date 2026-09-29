#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${SCRIPT_DIR}/pkg_root"
VERSION="1.0-1"
ARCH="amd64"
DEB_NAME="pack_${VERSION}_${ARCH}.deb"

echo "=== Building Pack / Unpack 1.0 from source ==="
cd "${SCRIPT_DIR}"
make clean
make -j$(nproc) all
make test

echo "=== Packaging DEB ==="
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/man/man1"
mkdir -p "${PKG_DIR}/usr/share/doc/pack"
mkdir -p "${PKG_DIR}/DEBIAN"

# Install stripped binaries
strip -s "${SCRIPT_DIR}/pack" -o "${PKG_DIR}/usr/bin/pack"
chmod 755 "${PKG_DIR}/usr/bin/pack"

strip -s "${SCRIPT_DIR}/unpack" -o "${PKG_DIR}/usr/bin/unpack"
chmod 755 "${PKG_DIR}/usr/bin/unpack"

ln -s unpack "${PKG_DIR}/usr/bin/pcat"

# Manpages
gzip -9c "${SCRIPT_DIR}/man/pack.1" > "${PKG_DIR}/usr/share/man/man1/pack.1.gz"
ln -s pack.1.gz "${PKG_DIR}/usr/share/man/man1/unpack.1.gz"
ln -s pack.1.gz "${PKG_DIR}/usr/share/man/man1/pcat.1.gz"

# Docs
cp "${SCRIPT_DIR}/README.md" "${PKG_DIR}/usr/share/doc/pack/"

# DEBIAN/control
cat << 'CTRL_EOF' > "${PKG_DIR}/DEBIAN/control"
Package: pack
Version: 1.0-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: seb3773 <99963207+seb3773@users.noreply.github.com>
Depends: libc6 (>= 2.15)
Provides: pack, unpack, pcat
Description: Historical Unix Huffman pack/unpack compression suite
 Pack is the canonical Huffman compression utility originally written by
 Thomas G. Szymanski (Bell Labs, 1978-1979) for Research Unix V8 / System V.
 It compresses single files into .z format and includes unpack and pcat.
 Modernized and packaged for Trinity Desktop Environment (TDE) / TdeZip companion tools.
CTRL_EOF

# Build DEB
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${SCRIPT_DIR}/${DEB_NAME}"
echo "Package created: ${SCRIPT_DIR}/${DEB_NAME}"

# Copy to pool
DEST_POOL="${SCRIPT_DIR}/../pool/main/p/pack"
mkdir -p "${DEST_POOL}"
cp -f "${SCRIPT_DIR}/${DEB_NAME}" "${DEST_POOL}/"
echo "Copied to ${DEST_POOL}/${DEB_NAME}"

# Clean temp packaging dir and build artifacts
rm -rf "${PKG_DIR}"
make clean

echo "=== Pack packaging finished successfully ==="
