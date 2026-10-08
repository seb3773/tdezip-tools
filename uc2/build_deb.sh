#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${SCRIPT_DIR}/pkg_root"
VERSION="3.0.0-1"
ARCH="amd64"
DEB_NAME="uc2_${VERSION}_${ARCH}.deb"

echo "=== Building UC2 from source ==="
"${SCRIPT_DIR}/build.sh"

echo "=== Packaging DEB ==="
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/man/man1"
mkdir -p "${PKG_DIR}/usr/share/doc/uc2"
mkdir -p "${PKG_DIR}/DEBIAN"

# Install stripped binary
strip -s "${SCRIPT_DIR}/build/cli/uc2" -o "${PKG_DIR}/usr/bin/uc2"
chmod 755 "${PKG_DIR}/usr/bin/uc2"

# Manpage
gzip -9c "${SCRIPT_DIR}/man/uc2.1" > "${PKG_DIR}/usr/share/man/man1/uc2.1.gz"

# Docs
cp "${SCRIPT_DIR}/README.md" "${PKG_DIR}/usr/share/doc/uc2/"
cp "${SCRIPT_DIR}/LICENSE" "${PKG_DIR}/usr/share/doc/uc2/"
cp "${SCRIPT_DIR}/CREDITS.md" "${PKG_DIR}/usr/share/doc/uc2/"

# DEBIAN/control
cat << 'CTRL_EOF' > "${PKG_DIR}/DEBIAN/control"
Package: uc2
Version: 3.0.0-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: seb3773 <99963207+seb3773@users.noreply.github.com>
Depends: libc6 (>= 2.15)
Provides: uc2
Description: UltraCompressor II archiver revival
 Modern C99 revival of UltraCompressor II, the DOS-era archiver by Nico
 de Vries (1992-1996). Features full backward compatibility with DOS UC2 Pro,
 content-defined chunking (CDC) deduplication, rANS entropy coding, and
 BLAKE3 archive integrity checking.
 Packaged for Trinity Desktop Environment (TDE) / TdeZip companion tools.
CTRL_EOF

# Build DEB
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${SCRIPT_DIR}/${DEB_NAME}"
echo "Package created: ${SCRIPT_DIR}/${DEB_NAME}"

# Copy to pool
DEST_POOL="${SCRIPT_DIR}/../pool/main/u/uc2"
mkdir -p "${DEST_POOL}"
cp -f "${SCRIPT_DIR}/${DEB_NAME}" "${DEST_POOL}/"
echo "Copied to ${DEST_POOL}/${DEB_NAME}"

# Clean temp packaging dir
rm -rf "${PKG_DIR}"

echo "=== UC2 packaging finished successfully ==="
