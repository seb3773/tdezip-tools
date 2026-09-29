#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="${SCRIPT_DIR}/.."
TMP_DIR=$(mktemp -d /tmp/pack_test_XXXXXX)

trap 'rm -rf "${TMP_DIR}"' EXIT

cd "${TMP_DIR}"

echo "=== Running Pack / Unpack / Pcat Test Suite ==="

# 1. Test short text
echo "TdeZip Historic Unix Pack (.z) Test String" > file1.txt
"${BIN_DIR}/pack" -f -v file1.txt
test -f file1.txt.z
test ! -f file1.txt

# 2. Decompress with pcat
"${BIN_DIR}/pcat" file1.txt.z > pcat_out.txt
diff -u <(echo "TdeZip Historic Unix Pack (.z) Test String") pcat_out.txt
echo "[PASS] pcat decompression verified"

# 3. Test gzip cross-compatibility
gzip -dc file1.txt.z > gzip_out.txt
diff -u <(echo "TdeZip Historic Unix Pack (.z) Test String") gzip_out.txt
echo "[PASS] gzip -dc decompression verified"

gzip -t file1.txt.z
echo "[PASS] gzip -t integrity check passed"

# 4. Decompress with unpack
"${BIN_DIR}/unpack" file1.txt.z
test -f file1.txt
test ! -f file1.txt.z
diff -u <(echo "TdeZip Historic Unix Pack (.z) Test String") file1.txt
echo "[PASS] unpack decompression verified"

# 5. Test stdout stream compression (-c)
"${BIN_DIR}/pack" -f -c file1.txt > file1_stdout.z
"${BIN_DIR}/pcat" file1_stdout.z > pcat_from_c.txt
diff -u file1.txt pcat_from_c.txt
echo "[PASS] pack -c stdout compression verified"

# 6. Test large data & bit-for-bit integrity
python3 -c "
with open('large.txt', 'w') as f:
    for i in range(5000):
        f.write(f'Line {i}: Research Unix Huffman pack tool verification.\n')
"
ORIG_HASH=$(sha256sum large.txt | cut -d' ' -f1)
"${BIN_DIR}/pack" -f large.txt
"${BIN_DIR}/pcat" large.txt.z > large_restored.txt
RESTORED_HASH=$(sha256sum large_restored.txt | cut -d' ' -f1)

if [ "$ORIG_HASH" != "$RESTORED_HASH" ]; then
    echo "[FAIL] SHA256 mismatch!"
    exit 1
fi
echo "[PASS] 5000 lines SHA256 verified ($ORIG_HASH)"

echo "=== All Pack / Unpack tests completed successfully! ==="
