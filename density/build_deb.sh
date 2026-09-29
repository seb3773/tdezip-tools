#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${SCRIPT_DIR}/pkg_root"
VERSION="1.0-1"
ARCH="amd64"
DEB_NAME="density_${VERSION}_${ARCH}.deb"

echo "=== Building Density from source ==="
cd "${SCRIPT_DIR}"
make clean
make -j$(nproc) all
make test

echo "=== Packaging DEB ==="
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/man/man1"
mkdir -p "${PKG_DIR}/usr/share/doc/density"
mkdir -p "${PKG_DIR}/DEBIAN"

# Install stripped binary
strip -s "${SCRIPT_DIR}/density" -o "${PKG_DIR}/usr/bin/density"
chmod 755 "${PKG_DIR}/usr/bin/density"

# Manpage
gzip -9c "${SCRIPT_DIR}/man/density.1" > "${PKG_DIR}/usr/share/man/man1/density.1.gz"

# Docs
cp "${SCRIPT_DIR}/README.md" "${PKG_DIR}/usr/share/doc/density/"
cp "${SCRIPT_DIR}/LICENSE" "${PKG_DIR}/usr/share/doc/density/copyright"

# DEBIAN/control
cat << 'CTRL_EOF' > "${PKG_DIR}/DEBIAN/control"
Package: density
Version: 1.0-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: seb3773 <99963207+seb3773@users.noreply.github.com>
Depends: libc6 (>= 2.15)
Provides: density
Description: Ultra-fast lossless data compression utility
 Density is an ultra-fast lossless data compression library and utility
 developed by Guillaume Vaudaux. Operating on 4-byte work units, it features
 three distinct algorithms (Chameleon, Cheetah, and Lion) delivering multi-gigabyte
 per second throughput while achieving balanced to high compression ratios.
 Framed in 2 MB streaming chunks with per-block IEEE 802.3 CRC32 verification.
 Packaged for Trinity Desktop Environment (TDE) / TdeZip companion tools.
CTRL_EOF

# Build DEB
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${SCRIPT_DIR}/${DEB_NAME}"
echo "Package created: ${SCRIPT_DIR}/${DEB_NAME}"

# Copy to pool
DEST_POOL="${SCRIPT_DIR}/../pool/main/d/density"
mkdir -p "${DEST_POOL}"
cp -f "${SCRIPT_DIR}/${DEB_NAME}" "${DEST_POOL}/"
echo "Copied to ${DEST_POOL}/${DEB_NAME}"

# Clean temp packaging dir and build artifacts
rm -rf "${PKG_DIR}"
make clean

echo "=== Density packaging finished successfully ==="
