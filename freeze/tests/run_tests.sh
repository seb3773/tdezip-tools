#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${SCRIPT_DIR}/../freeze"

if [ ! -x "${BIN}" ]; then
    echo "Error: ${BIN} not found or not executable. Run 'make' first." >&2
    exit 1
fi

TMP_DIR="$(mktemp -d /tmp/freeze_test_XXXXXX)"
trap "rm -rf ${TMP_DIR}" EXIT

echo "=== Running Freeze Test Suite ==="

SAMPLE="${TMP_DIR}/sample.txt"
FROZEN="${TMP_DIR}/sample.txt.F"
MELTED="${TMP_DIR}/sample_restored.txt"

cat << 'TXT_EOF' > "${SAMPLE}"
The quick brown fox jumps over the lazy dog. 1234567890!
Testing Freeze compression with repeated patterns:
FreezeFreezeFreezeFreezeFreezeFreezeFreezeFreezeFreezeFreeze!
Dynamic Huffman and LZSS sliding dictionary validation.
TXT_EOF

"${BIN}" -c "${SAMPLE}" > "${FROZEN}"
if [ ! -s "${FROZEN}" ]; then
    echo "FAIL: Compressed file is empty or missing." >&2
    exit 1
fi
echo "OK: Compression successful ($(wc -c < "${SAMPLE}") -> $(wc -c < "${FROZEN}") bytes)"

# Test 2: Integrity check (-dc to /dev/null)
"${BIN}" -dc "${FROZEN}" > /dev/null
echo "OK: Integrity check (-dc) passed"

# Test 3: Decompression and comparison
"${BIN}" -dc "${FROZEN}" > "${MELTED}"
if ! cmp -s "${SAMPLE}" "${MELTED}"; then
    echo "FAIL: Restored file does not match original." >&2
    exit 1
fi
echo "OK: Decompressed file matches original bit-for-bit"

echo "=== All Freeze tests passed successfully ==="
