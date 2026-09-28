#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${SCRIPT_DIR}/pkg_root"
VERSION="2.5.0-1"
ARCH="amd64"
DEB_NAME="freeze_${VERSION}_${ARCH}.deb"

echo "=== Building Freeze 2.5.0 from source ==="
cd "${SCRIPT_DIR}"
make clean
make -j$(nproc) all
make test

echo "=== Packaging DEB ==="
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/man/man1"
mkdir -p "${PKG_DIR}/usr/share/doc/freeze"
mkdir -p "${PKG_DIR}/DEBIAN"

# Install stripped binaries
strip -s "${SCRIPT_DIR}/freeze" -o "${PKG_DIR}/usr/bin/freeze"
chmod 755 "${PKG_DIR}/usr/bin/freeze"

if [ -f "${SCRIPT_DIR}/statist" ]; then
    strip -s "${SCRIPT_DIR}/statist" -o "${PKG_DIR}/usr/bin/statist"
    chmod 755 "${PKG_DIR}/usr/bin/statist"
fi

# Symlinks for melt, fcat and unfreeze
ln -s freeze "${PKG_DIR}/usr/bin/melt"
ln -s freeze "${PKG_DIR}/usr/bin/fcat"
ln -s freeze "${PKG_DIR}/usr/bin/unfreeze"

# Manpages
gzip -9c "${SCRIPT_DIR}/man/freeze.1" > "${PKG_DIR}/usr/share/man/man1/freeze.1.gz"
ln -s freeze.1.gz "${PKG_DIR}/usr/share/man/man1/melt.1.gz"
ln -s freeze.1.gz "${PKG_DIR}/usr/share/man/man1/fcat.1.gz"
if [ -f "${SCRIPT_DIR}/man/statist.1" ]; then
    gzip -9c "${SCRIPT_DIR}/man/statist.1" > "${PKG_DIR}/usr/share/man/man1/statist.1.gz"
fi

# Docs
cp "${SCRIPT_DIR}/README.md" "${PKG_DIR}/usr/share/doc/freeze/"
if [ -f "${SCRIPT_DIR}/docs/README" ]; then
    cp "${SCRIPT_DIR}/docs/README" "${PKG_DIR}/usr/share/doc/freeze/"
fi

# DEBIAN/control
cat << 'CTRL_EOF' > "${PKG_DIR}/DEBIAN/control"
Package: freeze
Version: 2.5.0-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: seb3773 <99963207+seb3773@users.noreply.github.com>
Depends: libc6 (>= 2.15)
Provides: freeze, melt
Description: Freeze and melt compression / decompression utility
 Freeze is an archive compression program developed by Leonid Broukhis.
 It combines a modified LZSS algorithm (sliding dictionary of 8192 bytes)
 with dynamic Huffman coding, providing significant compression efficiency
 over traditional Unix compress.
 Packaged for Trinity Desktop Environment (TDE) / TdeZip companion tools.
CTRL_EOF

# Build DEB
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${SCRIPT_DIR}/${DEB_NAME}"
echo "Package created: ${SCRIPT_DIR}/${DEB_NAME}"

# Copy to pool
DEST_POOL="${SCRIPT_DIR}/../pool/main/f/freeze"
mkdir -p "${DEST_POOL}"
cp -f "${SCRIPT_DIR}/${DEB_NAME}" "${DEST_POOL}/"
echo "Copied to ${DEST_POOL}/${DEB_NAME}"

# Clean temp packaging dir and build artifacts
rm -rf "${PKG_DIR}"
make clean

echo "=== Freeze packaging finished successfully ==="
