#!/bin/bash
set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="${REPO_ROOT}/dists/stable"
BIN_AMD64="${DIST_DIR}/main/binary-amd64"

echo "=== Updating TdeZip APT Repository ==="
mkdir -p "${BIN_AMD64}"

cd "${REPO_ROOT}"

# Generate Packages file using dpkg-scanpackages or apt-ftparchive
if command -v dpkg-scanpackages >/dev/null 2>&1; then
    echo "Scanning packages with dpkg-scanpackages..."
    dpkg-scanpackages --multiversion pool/ > "${BIN_AMD64}/Packages"
elif command -v apt-ftparchive >/dev/null 2>&1; then
    echo "Scanning packages with apt-ftparchive..."
    apt-ftparchive packages pool/ > "${BIN_AMD64}/Packages"
else
    echo "Error: Neither dpkg-scanpackages nor apt-ftparchive found!" >&2
    exit 1
fi

# Compress Packages
gzip -9c "${BIN_AMD64}/Packages" > "${BIN_AMD64}/Packages.gz"

echo "Packages size: $(wc -c < "${BIN_AMD64}/Packages") bytes"
echo "Packages.gz size: $(wc -c < "${BIN_AMD64}/Packages.gz") bytes"

# Generate Release file
DATE_UTC="$(date -Ru)"

cat << REL_EOF > "${DIST_DIR}/Release"
Architectures: amd64
Codename: stable
Components: main
Date: ${DATE_UTC}
Description: Dedicated Tools Repository for TdeZip / PeaZip (Trinity Desktop Environment)
Label: TdeZip-Tools APT Repository
Origin: TdeZip-Tools
Suite: stable
REL_EOF

# Calculate checksums for Release
echo "MD5Sum:" >> "${DIST_DIR}/Release"
for file in main/binary-amd64/Packages main/binary-amd64/Packages.gz; do
    if [ -f "${DIST_DIR}/${file}" ]; then
        hash="$(md5sum "${DIST_DIR}/${file}" | awk '{print $1}')"
        size="$(wc -c < "${DIST_DIR}/${file}")"
        printf " %s %16d %s\n" "${hash}" "${size}" "${file}" >> "${DIST_DIR}/Release"
    fi
done

echo "SHA1:" >> "${DIST_DIR}/Release"
for file in main/binary-amd64/Packages main/binary-amd64/Packages.gz; do
    if [ -f "${DIST_DIR}/${file}" ]; then
        hash="$(sha1sum "${DIST_DIR}/${file}" | awk '{print $1}')"
        size="$(wc -c < "${DIST_DIR}/${file}")"
        printf " %s %16d %s\n" "${hash}" "${size}" "${file}" >> "${DIST_DIR}/Release"
    fi
done

echo "SHA256:" >> "${DIST_DIR}/Release"
for file in main/binary-amd64/Packages main/binary-amd64/Packages.gz; do
    if [ -f "${DIST_DIR}/${file}" ]; then
        hash="$(sha256sum "${DIST_DIR}/${file}" | awk '{print $1}')"
        size="$(wc -c < "${DIST_DIR}/${file}")"
        printf " %s %16d %s\n" "${hash}" "${size}" "${file}" >> "${DIST_DIR}/Release"
    fi
done

echo "SHA512:" >> "${DIST_DIR}/Release"
for file in main/binary-amd64/Packages main/binary-amd64/Packages.gz; do
    if [ -f "${DIST_DIR}/${file}" ]; then
        hash="$(sha512sum "${DIST_DIR}/${file}" | awk '{print $1}')"
        size="$(wc -c < "${DIST_DIR}/${file}")"
        printf " %s %16d %s\n" "${hash}" "${size}" "${file}" >> "${DIST_DIR}/Release"
    fi
done

echo "=== Repository successfully updated! ==="
