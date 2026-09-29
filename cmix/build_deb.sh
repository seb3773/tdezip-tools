#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${SCRIPT_DIR}/pkg_root"
VERSION="21-1"
ARCH="amd64"
DEB_NAME="cmix_${VERSION}_${ARCH}.deb"

echo "=== Building CMIX from source ==="
cd "${SCRIPT_DIR}"
make clean
make -j$(nproc) all
make test

echo "=== Packaging DEB ==="
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/cmix/dictionary"
mkdir -p "${PKG_DIR}/usr/share/man/man1"
mkdir -p "${PKG_DIR}/usr/share/doc/cmix"
mkdir -p "${PKG_DIR}/DEBIAN"

# Install stripped binary
strip -s "${SCRIPT_DIR}/cmix" -o "${PKG_DIR}/usr/bin/cmix"
chmod 755 "${PKG_DIR}/usr/bin/cmix"

# Dictionary
if [ -f "${SCRIPT_DIR}/dictionary/english.dic" ]; then
    cp "${SCRIPT_DIR}/dictionary/english.dic" "${PKG_DIR}/usr/share/cmix/dictionary/"
fi

# Manpage
gzip -9c "${SCRIPT_DIR}/man/cmix.1" > "${PKG_DIR}/usr/share/man/man1/cmix.1.gz"

# Docs
cp "${SCRIPT_DIR}/README.md" "${PKG_DIR}/usr/share/doc/cmix/"
cp "${SCRIPT_DIR}/LICENSE" "${PKG_DIR}/usr/share/doc/cmix/copyright"

# DEBIAN/control
cat << 'CTRL_EOF' > "${PKG_DIR}/DEBIAN/control"
Package: cmix
Version: 21-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: seb3773 <99963207+seb3773@users.noreply.github.com>
Depends: libc6 (>= 2.34), libstdc++6 (>= 11)
Provides: cmix
Description: Extreme lossless data compression program using context mixing
 CMIX is a lossless data compression program developed by Byron Knoll
 that achieves state-of-the-art compression ratios by combining an ensemble
 of diverse context models (PPM, PAQ8, indirect hashes, interval models)
 through a recurrent LSTM neural network. Designed for maximum ratio benchmarks.
 Packaged for Trinity Desktop Environment (TDE) / TdeZip companion tools.
CTRL_EOF

# Build DEB
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${SCRIPT_DIR}/${DEB_NAME}"
echo "Package created: ${SCRIPT_DIR}/${DEB_NAME}"

# Copy to pool
DEST_POOL="${SCRIPT_DIR}/../pool/main/c/cmix"
mkdir -p "${DEST_POOL}"
cp -f "${SCRIPT_DIR}/${DEB_NAME}" "${DEST_POOL}/"
echo "Copied to ${DEST_POOL}/${DEB_NAME}"

# Clean temp packaging dir and build artifacts
rm -rf "${PKG_DIR}"
make clean

echo "=== CMIX packaging finished successfully ==="
