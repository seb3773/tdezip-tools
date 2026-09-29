#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${SCRIPT_DIR}/pkg_root"
VERSION="1.0-1"
ARCH="amd64"
DEB_NAME="lzfse_${VERSION}_${ARCH}.deb"

echo "=== Building LZFSE from source ==="
cd "${SCRIPT_DIR}"
make clean
make -j$(nproc) all
make test

echo "=== Packaging DEB ==="
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/man/man1"
mkdir -p "${PKG_DIR}/usr/share/doc/lzfse"
mkdir -p "${PKG_DIR}/DEBIAN"

# Install stripped binary
strip -s "${SCRIPT_DIR}/lzfse" -o "${PKG_DIR}/usr/bin/lzfse"
chmod 755 "${PKG_DIR}/usr/bin/lzfse"

# Manpage
gzip -9c "${SCRIPT_DIR}/man/lzfse.1" > "${PKG_DIR}/usr/share/man/man1/lzfse.1.gz"

# Docs
cp "${SCRIPT_DIR}/README.md" "${PKG_DIR}/usr/share/doc/lzfse/"

# DEBIAN/control
cat << 'CTRL_EOF' > "${PKG_DIR}/DEBIAN/control"
Package: lzfse
Version: 1.0-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: seb3773 <99963207+seb3773@users.noreply.github.com>
Depends: libc6 (>= 2.15)
Provides: lzfse
Description: Apple LZFSE lossless data compression utility
 LZFSE is a high-speed lossless data compression algorithm developed by
 Apple Inc. using Lempel-Ziv sliding window and Finite State Entropy (FSE).
 It provides compression ratios comparable to Deflate/zlib while offering
 2x to 3x higher throughput with low energy consumption.
 Packaged for Trinity Desktop Environment (TDE) / TdeZip companion tools.
CTRL_EOF

# Build DEB
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${SCRIPT_DIR}/${DEB_NAME}"
echo "Package created: ${SCRIPT_DIR}/${DEB_NAME}"

# Copy to pool
DEST_POOL="${SCRIPT_DIR}/../pool/main/l/lzfse"
mkdir -p "${DEST_POOL}"
cp -f "${SCRIPT_DIR}/${DEB_NAME}" "${DEST_POOL}/"
echo "Copied to ${DEST_POOL}/${DEB_NAME}"

# Clean temp packaging dir and build artifacts
rm -rf "${PKG_DIR}"
make clean

echo "=== LZFSE packaging finished successfully ==="
