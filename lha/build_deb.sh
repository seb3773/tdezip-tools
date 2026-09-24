#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_DIR="${SCRIPT_DIR}/pkg_root"
VERSION="1.14i-1"
ARCH="amd64"
DEB_NAME="lha_${VERSION}_${ARCH}.deb"

echo "=== Building LHA from source ==="
cd "${SCRIPT_DIR}"
./configure --prefix=/usr
make -j$(nproc)

echo "=== Packaging DEB ==="
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/bin"
mkdir -p "${PKG_DIR}/usr/share/man/man1"
mkdir -p "${PKG_DIR}/usr/share/doc/lha"
mkdir -p "${PKG_DIR}/DEBIAN"

# Install stripped binary as lha.bin (update-alternatives targets lha)
strip -s "${SCRIPT_DIR}/src/lha" -o "${PKG_DIR}/usr/bin/lha.bin"
chmod 755 "${PKG_DIR}/usr/bin/lha.bin"

# Man page
gzip -9c "${SCRIPT_DIR}/man/lha.man" > "${PKG_DIR}/usr/share/man/man1/lha-classic.1.gz"
chmod 644 "${PKG_DIR}/usr/share/man/man1/lha-classic.1.gz"

# Docs
cp "${SCRIPT_DIR}/README.md" "${PKG_DIR}/usr/share/doc/lha/"
cp "${SCRIPT_DIR}/header.doc.md" "${PKG_DIR}/usr/share/doc/lha/"

# DEBIAN/control
cat << 'CTRL_EOF' > "${PKG_DIR}/DEBIAN/control"
Package: lha
Version: 1.14i-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: seb3773 <99963207+seb3773@users.noreply.github.com>
Depends: libc6 (>= 2.15)
Provides: lha
Description: LHA archive compression and extraction utility (native C)
 LHa is an archive and compression utility for .lzh and .lha files.
 Unlike extraction-only tools (like lhasa), this native C package supports
 full archive creation (-a), updating (-u), testing (-t) and extraction (-x).
 Compiled for Trinity Desktop Environment (TDE) / TdeZip archiver.
CTRL_EOF

# DEBIAN/postinst
cat << 'POST_EOF' > "${PKG_DIR}/DEBIAN/postinst"
#!/bin/sh
set -e

if [ "$1" = "configure" ]; then
    update-alternatives --install /usr/bin/lha lha /usr/bin/lha.bin 100 \
        --slave /usr/share/man/man1/lha.1.gz lha.1.gz /usr/share/man/man1/lha-classic.1.gz
fi

exit 0
POST_EOF
chmod 755 "${PKG_DIR}/DEBIAN/postinst"

# DEBIAN/prerm
cat << 'PRE_EOF' > "${PKG_DIR}/DEBIAN/prerm"
#!/bin/sh
set -e

if [ "$1" = "remove" ] || [ "$1" = "deconfigure" ]; then
    update-alternatives --remove lha /usr/bin/lha.bin
fi

exit 0
PRE_EOF
chmod 755 "${PKG_DIR}/DEBIAN/prerm"

# Build DEB
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${SCRIPT_DIR}/${DEB_NAME}"
echo "Package created: ${SCRIPT_DIR}/${DEB_NAME}"

# Copy to pool
DEST_POOL="${SCRIPT_DIR}/../pool/main/l/lha"
mkdir -p "${DEST_POOL}"
cp -f "${SCRIPT_DIR}/${DEB_NAME}" "${DEST_POOL}/"
echo "Copied to ${DEST_POOL}/${DEB_NAME}"

# Clean temp packaging dir
rm -rf "${PKG_DIR}"
