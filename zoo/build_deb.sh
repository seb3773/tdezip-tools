#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${SCRIPT_DIR}/pkg_root"
VERSION="2.10-28"
ARCH="amd64"
DEB_NAME="zoo_${VERSION}_${ARCH}.deb"

echo "=== Building ZOO 2.10 from source ==="
cd "${SCRIPT_DIR}"
make clean
make -j$(nproc) all
make test

echo "=== Packaging DEB ==="
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/man/man1"
mkdir -p "${PKG_DIR}/usr/share/doc/zoo"
mkdir -p "${PKG_DIR}/DEBIAN"

# Install stripped binaries
strip -s "${SCRIPT_DIR}/zoo" -o "${PKG_DIR}/usr/bin/zoo"
chmod 755 "${PKG_DIR}/usr/bin/zoo"

strip -s "${SCRIPT_DIR}/fiz" -o "${PKG_DIR}/usr/bin/fiz"
chmod 755 "${PKG_DIR}/usr/bin/fiz"

# Manpages
gzip -9c "${SCRIPT_DIR}/man/zoo.1" > "${PKG_DIR}/usr/share/man/man1/zoo.1.gz"
gzip -9c "${SCRIPT_DIR}/man/fiz.1" > "${PKG_DIR}/usr/share/man/man1/fiz.1.gz"

# Docs
if [ -f "${SCRIPT_DIR}/README.md" ]; then
    cp "${SCRIPT_DIR}/README.md" "${PKG_DIR}/usr/share/doc/zoo/"
fi
if [ -f "${SCRIPT_DIR}/docs/README.Debian" ]; then
    cp "${SCRIPT_DIR}/docs/README.Debian" "${PKG_DIR}/usr/share/doc/zoo/"
fi
if [ -f "${SCRIPT_DIR}/docs/copyright" ]; then
    cp "${SCRIPT_DIR}/docs/copyright" "${PKG_DIR}/usr/share/doc/zoo/"
fi
if [ -f "${SCRIPT_DIR}/docs/changelog" ]; then
    gzip -9c "${SCRIPT_DIR}/docs/changelog" > "${PKG_DIR}/usr/share/doc/zoo/changelog.Debian.gz"
fi

# DEBIAN/control
cat << 'CTRL_EOF' > "${PKG_DIR}/DEBIAN/control"
Package: zoo
Version: 2.10-28
Section: utils
Priority: optional
Architecture: amd64
Maintainer: seb3773 <99963207+seb3773@users.noreply.github.com>
Depends: libc6 (>= 2.14)
Provides: zoo, fiz
Description: Rahul Dhesi ZOO archive manipulation and recovery tool
 Zoo is used to create and maintain collections of files in compressed
 form using Lempel-Ziv compression (LZW) with support for generations,
 full path preservation, comments and integrity checking.
 Includes 'fiz' for repairing and analyzing damaged zoo archives.
 Packaged with POSIX and 64-bit security patches for Trinity Desktop
 Environment (TDE) / TdeZip companion tools.
CTRL_EOF

# Build DEB
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${SCRIPT_DIR}/${DEB_NAME}"
echo "Package created: ${SCRIPT_DIR}/${DEB_NAME}"

# Copy to pool
DEST_POOL="${SCRIPT_DIR}/../pool/main/z/zoo"
mkdir -p "${DEST_POOL}"
cp -f "${SCRIPT_DIR}/${DEB_NAME}" "${DEST_POOL}/"
echo "Copied to ${DEST_POOL}/${DEB_NAME}"

# Clean temp packaging dir and build artifacts
rm -rf "${PKG_DIR}"
make clean

echo "=== ZOO packaging finished successfully ==="
