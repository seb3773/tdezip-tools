#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LZAV="${SCRIPT_DIR}/../lzav"
TEST_TMP="${SCRIPT_DIR}/test_tmp"

echo "=== Running LZAV Test Suite ==="

rm -rf "${TEST_TMP}"
mkdir -p "${TEST_TMP}"
trap 'rm -rf "${TEST_TMP}"' EXIT

# Test 1: Basic string pipeline compression & decompression
echo "Test 1: Standard stream pipeline..."
ORIG_STR="The quick brown fox jumps over the lazy dog! 1234567890."
REC_STR=$(echo "${ORIG_STR}" | "${LZAV}" | "${LZAV}" -d)
if [ "${ORIG_STR}" != "${REC_STR}" ]; then
    echo "FAIL: stream pipeline mismatch"
    exit 1
fi
echo "  [PASS] Stream pipeline passed."

# Test 2: File mode compression and decompression (default mode)
echo "Test 2: File mode with default compression..."
head -c 250000 /dev/urandom > "${TEST_TMP}/sample.bin"
"${LZAV}" -i "${TEST_TMP}/sample.bin" -o "${TEST_TMP}/sample.bin.lzav"
"${LZAV}" -t "${TEST_TMP}/sample.bin.lzav"
"${LZAV}" -d -i "${TEST_TMP}/sample.bin.lzav" -o "${TEST_TMP}/sample_dec.bin"
cmp "${TEST_TMP}/sample.bin" "${TEST_TMP}/sample_dec.bin"
echo "  [PASS] File mode default passed."

# Test 3: HI mode (-2 / --best)
echo "Test 3: HI mode compression (-2)..."
"${LZAV}" -2 -i "${TEST_TMP}/sample.bin" -o "${TEST_TMP}/sample_hi.bin.lzav"
"${LZAV}" -t "${TEST_TMP}/sample_hi.bin.lzav"
"${LZAV}" -d -i "${TEST_TMP}/sample_hi.bin.lzav" -o "${TEST_TMP}/sample_hi_dec.bin"
cmp "${TEST_TMP}/sample.bin" "${TEST_TMP}/sample_hi_dec.bin"
echo "  [PASS] HI mode passed."

# Test 4: GNU Tar integration
echo "Test 4: GNU Tar integration (--use-compress-program)..."
mkdir -p "${TEST_TMP}/tar_in"
echo "Alpha content 123" > "${TEST_TMP}/tar_in/a.txt"
echo "Beta content 456" > "${TEST_TMP}/tar_in/b.txt"
tar --use-compress-program="${LZAV}" -cf "${TEST_TMP}/archive.tar.lzav" -C "${TEST_TMP}/tar_in" a.txt b.txt
tar --use-compress-program="${LZAV}" -tf "${TEST_TMP}/archive.tar.lzav"
mkdir -p "${TEST_TMP}/tar_out"
tar --use-compress-program="${LZAV}" -xf "${TEST_TMP}/archive.tar.lzav" -C "${TEST_TMP}/tar_out"
cmp "${TEST_TMP}/tar_in/a.txt" "${TEST_TMP}/tar_out/a.txt"
cmp "${TEST_TMP}/tar_in/b.txt" "${TEST_TMP}/tar_out/b.txt"
echo "  [PASS] Tar integration passed."

# Test 5: Corruption detection
echo "Test 5: Corruption detection..."
printf "BAD_DATA" | dd of="${TEST_TMP}/sample.bin.lzav" bs=1 seek=32 conv=notrunc 2>/dev/null
if "${LZAV}" -t "${TEST_TMP}/sample.bin.lzav" 2>/dev/null; then
    echo "FAIL: Corruption was not detected!"
    exit 1
fi
echo "  [PASS] Corruption detected properly."

echo "=== All LZAV tests passed successfully! ==="
