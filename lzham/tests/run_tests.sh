#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LZHAM_BIN="${SCRIPT_DIR}/../lzham"
TEST_TMP="${SCRIPT_DIR}/tmp_test"

if [ ! -x "${LZHAM_BIN}" ]; then
    echo "Error: lzham binary not found or not executable at ${LZHAM_BIN}" >&2
    exit 1
fi

rm -rf "${TEST_TMP}"
mkdir -p "${TEST_TMP}"
cd "${TEST_TMP}"

echo "1. Generating test payload..."
echo "LZHAM compression test payload with numbers 0123456789 and symbols !@#$%^&*()" > file1.txt
head -c 131072 /dev/urandom > file2.bin

echo "2. Testing file compression..."
"${LZHAM_BIN}" -f file1.txt
"${LZHAM_BIN}" -f file2.bin
if [ ! -f "file1.txt.lzham" ] || [ ! -f "file2.bin.lzham" ]; then
    echo "Error: Compressed files not created!" >&2
    exit 1
fi

echo "3. Testing archive integrity (-t)..."
"${LZHAM_BIN}" -t file1.txt.lzham
"${LZHAM_BIN}" -t file2.bin.lzham

echo "4. Testing file decompression (-d -f)..."
"${LZHAM_BIN}" -d -f file1.txt.lzham
"${LZHAM_BIN}" -d -f file2.bin.lzham
cmp file1.txt file1.txt
cmp file2.bin file2.bin

echo "5. Testing pipe streaming (stdin -> lzham -c -> lzham -dc -> stdout)..."
cat file1.txt | "${LZHAM_BIN}" -c | "${LZHAM_BIN}" -dc > pipe_restored.txt
cmp file1.txt pipe_restored.txt

echo "6. Testing Tar multi-file archive integration..."
tar --use-compress-program="${LZHAM_BIN}" -cf archive.tar.lzham file1.txt file2.bin
mkdir tar_out
cd tar_out
tar --use-compress-program="${LZHAM_BIN} -d" -xf ../archive.tar.lzham
cmp ../file1.txt file1.txt
cmp ../file2.bin file2.bin
cd ..

echo "7. Testing legacy syntax (lzham c in out / lzham d in out)..."
"${LZHAM_BIN}" c file1.txt legacy.lzham
"${LZHAM_BIN}" d legacy.lzham legacy_out.txt
cmp file1.txt legacy_out.txt

cd "${SCRIPT_DIR}"
rm -rf "${TEST_TMP}"

echo "=== All LZHAM tests passed successfully! ==="
