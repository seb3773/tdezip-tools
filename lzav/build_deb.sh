#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${SCRIPT_DIR}/pkg_root"
VERSION="1.0-1"
ARCH="amd64"
DEB_NAME="lzav_${VERSION}_${ARCH}.deb"

echo "=== Building LZAV from source ==="
cd "${SCRIPT_DIR}"
make clean
make -j$(nproc) all
make test

echo "=== Packaging DEB ==="
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/man/man1"
mkdir -p "${PKG_DIR}/usr/share/doc/lzav"
mkdir -p "${PKG_DIR}/DEBIAN"

# Install stripped binary
strip -s "${SCRIPT_DIR}/lzav" -o "${PKG_DIR}/usr/bin/lzav"
chmod 755 "${PKG_DIR}/usr/bin/lzav"

# Manpage
gzip -9c "${SCRIPT_DIR}/man/lzav.1" > "${PKG_DIR}/usr/share/man/man1/lzav.1.gz"

# Docs
cp "${SCRIPT_DIR}/README.md" "${PKG_DIR}/usr/share/doc/lzav/"

# DEBIAN/control
cat << 'CTRL_EOF' > "${PKG_DIR}/DEBIAN/control"
Package: lzav
Version: 1.0-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: seb3773 <99963207+seb3773@users.noreply.github.com>
Depends: libc6 (>= 2.15)
Provides: lzav
Description: Fast in-memory LZ77 lossless data compression utility
 LZAV is a fast, general-purpose in-memory lossless data compression
 algorithm developed by Aleksey Vaneev. It achieves superior compression
 ratios to LZ4/Snappy while delivering multi-gigabyte per second decompression
 speeds with built-in out-of-bounds safety checks.
 Packaged for Trinity Desktop Environment (TDE) / TdeZip companion tools.
CTRL_EOF

# Build DEB
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${SCRIPT_DIR}/${DEB_NAME}"
echo "Package created: ${SCRIPT_DIR}/${DEB_NAME}"

# Copy to pool
DEST_POOL="${SCRIPT_DIR}/../pool/main/l/lzav"
mkdir -p "${DEST_POOL}"
cp -f "${SCRIPT_DIR}/${DEB_NAME}" "${DEST_POOL}/"
echo "Copied to ${DEST_POOL}/${DEB_NAME}"

# Clean temp packaging dir and build artifacts
rm -rf "${PKG_DIR}"
make clean

echo "=== LZAV packaging finished successfully ==="
