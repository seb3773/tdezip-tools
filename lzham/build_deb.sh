#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${SCRIPT_DIR}/pkg_root"
VERSION="1.0-1"
ARCH="amd64"
DEB_NAME="lzham_${VERSION}_${ARCH}.deb"

echo "=== Building LZHAM 1.0 from source ==="
cd "${SCRIPT_DIR}"
make clean
make -j$(nproc) all
make test

echo "=== Packaging DEB ==="
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/man/man1"
mkdir -p "${PKG_DIR}/usr/share/doc/lzham"
mkdir -p "${PKG_DIR}/DEBIAN"

# Install stripped binary
strip -s "${SCRIPT_DIR}/lzham" -o "${PKG_DIR}/usr/bin/lzham"
chmod 755 "${PKG_DIR}/usr/bin/lzham"

# Manpage
gzip -9c "${SCRIPT_DIR}/man/lzham.1" > "${PKG_DIR}/usr/share/man/man1/lzham.1.gz"

# Docs
if [ -f "${SCRIPT_DIR}/README.md" ]; then
    cp "${SCRIPT_DIR}/README.md" "${PKG_DIR}/usr/share/doc/lzham/"
fi
if [ -f "${SCRIPT_DIR}/docs/LICENSE" ]; then
    cp "${SCRIPT_DIR}/docs/LICENSE" "${PKG_DIR}/usr/share/doc/lzham/"
fi
if [ -f "${SCRIPT_DIR}/docs/UPSTREAM_README.md" ]; then
    cp "${SCRIPT_DIR}/docs/UPSTREAM_README.md" "${PKG_DIR}/usr/share/doc/lzham/"
fi

# DEBIAN/control
cat << 'CTRL_EOF' > "${PKG_DIR}/DEBIAN/control"
Package: lzham
Version: 1.0-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: seb3773 <99963207+seb3773@users.noreply.github.com>
Depends: libc6 (>= 2.15), libstdc++6 (>= 5.2)
Provides: lzham
Description: Richard Geldreich LZHAM high-ratio lossless compression utility
 LZHAM is a lossless data compression codec with compression ratios
 comparable to LZMA and faster decompression throughput. It supports
 dictionary sizes up to 512 MB and multi-threaded execution.
 Includes native command-line interface for individual files, Unix
 pipelines and Tar multi-file archives (.tar.lzham).
 Packaged for Trinity Desktop Environment (TDE) / TdeZip companion tools.
CTRL_EOF

# Build DEB
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${SCRIPT_DIR}/${DEB_NAME}"
echo "Package created: ${SCRIPT_DIR}/${DEB_NAME}"

# Copy to pool
DEST_POOL="${SCRIPT_DIR}/../pool/main/l/lzham"
mkdir -p "${DEST_POOL}"
cp -f "${SCRIPT_DIR}/${DEB_NAME}" "${DEST_POOL}/"
echo "Copied to ${DEST_POOL}/${DEB_NAME}"

# Clean temp packaging dir and build artifacts
rm -rf "${PKG_DIR}"
make clean

echo "=== LZHAM packaging finished successfully ==="
