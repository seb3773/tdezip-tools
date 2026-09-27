#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${SCRIPT_DIR}/pkg_root"
VERSION="1.0-1"
ARCH="amd64"
DEB_NAME="pea-c_${VERSION}_${ARCH}.deb"

echo "=== Building PEA-C from source ==="
cd "${SCRIPT_DIR}"
make clean
make -j$(nproc) all

echo "=== Packaging DEB ==="
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/doc/pea-c"
mkdir -p "${PKG_DIR}/DEBIAN"

# Install stripped binary directly as /usr/bin/pea-c
strip -s "${SCRIPT_DIR}/pea" -o "${PKG_DIR}/usr/bin/pea-c"
chmod 755 "${PKG_DIR}/usr/bin/pea-c"

# Docs
cp "${SCRIPT_DIR}/README.md" "${PKG_DIR}/usr/share/doc/pea-c/"
if [ -d "${SCRIPT_DIR}/docs" ]; then
    cp -r "${SCRIPT_DIR}/docs" "${PKG_DIR}/usr/share/doc/pea-c/"
fi

# DEBIAN/control
cat << 'CTRL_EOF' > "${PKG_DIR}/DEBIAN/control"
Package: pea-c
Version: 1.0-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: seb3773 <99963207+seb3773@users.noreply.github.com>
Depends: libc6 (>= 2.15), zlib1g (>= 1:1.1.4), libssl3 (>= 3.0.0), libnettle8 (>= 3.8)
Provides: pea-c
Description: PEA archive format compression and extraction engine (native C)
 Pure native C implementation of the PEA archive format (version 1.6),
 packaged as a dedicated, ultra-lightweight (52 KB) standalone tool
 for Trinity Desktop Environment (TDE) / TdeZip archiver.
 Supports store and deflate compression, multiple checksum algorithms
 (CRC32, SHA256, Adler32, Blake2...), authenticated encryption
 (EAX, EAX256, HMAC, Twofish, Serpent, cascades), keyfiles, and multi-volume archives.
CTRL_EOF

# Build DEB
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${SCRIPT_DIR}/${DEB_NAME}"
echo "Package created: ${SCRIPT_DIR}/${DEB_NAME}"

# Copy to pool
DEST_POOL="${SCRIPT_DIR}/../pool/main/p/pea-c"
mkdir -p "${DEST_POOL}"
cp -f "${SCRIPT_DIR}/${DEB_NAME}" "${DEST_POOL}/"
echo "Copied to ${DEST_POOL}/${DEB_NAME}"

# Clean temp packaging dir
rm -rf "${PKG_DIR}"

echo "=== PEA-C packaging finished successfully ==="
