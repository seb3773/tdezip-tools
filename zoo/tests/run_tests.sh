#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZOO_BIN="${SCRIPT_DIR}/../zoo"
FIZ_BIN="${SCRIPT_DIR}/../fiz"
TEST_TMP="${SCRIPT_DIR}/tmp_test"

if [ ! -x "${ZOO_BIN}" ]; then
    echo "Error: zoo binary not found or not executable at ${ZOO_BIN}" >&2
    exit 1
fi

rm -rf "${TEST_TMP}"
mkdir -p "${TEST_TMP}"
cd "${TEST_TMP}"

echo "1. Creating test payload..."
echo "Hello ZOO Archive Test 1234567890" > sample1.txt
head -c 65536 /dev/urandom > sample2.bin

echo "2. Creating archive test.zoo..."
"${ZOO_BIN}" -add test.zoo sample1.txt sample2.bin

echo "3. Testing archive listing..."
"${ZOO_BIN}" -list test.zoo

echo "4. Testing archive integrity..."
"${ZOO_BIN}" -test test.zoo

echo "5. Testing archive extraction..."
mkdir out
cd out
"${ZOO_BIN}" -extract ../test.zoo
cd ..

echo "6. Verifying extracted payload integrity..."
cmp sample1.txt out/sample1.txt
cmp sample2.bin out/sample2.bin

if [ -x "${FIZ_BIN}" ]; then
    echo "7. Testing fiz integrity scanner on test.zoo..."
    "${FIZ_BIN}" test.zoo
fi

cd "${SCRIPT_DIR}"
rm -rf "${TEST_TMP}"

echo "=== All ZOO tests passed successfully! ==="
