#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="${SCRIPT_DIR}/.."
TMP_DIR=$(mktemp -d /tmp/lzfse_test_XXXXXX)

trap 'rm -rf "${TMP_DIR}"' EXIT

cd "${TMP_DIR}"

echo "=== Running LZFSE Test Suite ==="

# 1. Test short text compression
echo "Apple LZFSE algorithm test string for TdeZip companion tools." > sample.txt
"${BIN_DIR}/lzfse" -encode -i sample.txt -o sample.txt.lzfse
test -f sample.txt.lzfse

# 2. Test decode (Apple syntax)
"${BIN_DIR}/lzfse" -decode -i sample.txt.lzfse -o sample_restored.txt
diff -u sample.txt sample_restored.txt
echo "[PASS] Apple syntax encode / decode verified"

# 3. Test integrity test (-t)
"${BIN_DIR}/lzfse" -t sample.txt.lzfse
echo "[PASS] Archive integrity test (-t) verified"

# 4. Test decode (Unix syntax -d)
"${BIN_DIR}/lzfse" -d sample.txt.lzfse -o sample_unix.txt
diff -u sample.txt sample_unix.txt
echo "[PASS] Unix syntax (-d) verified"

# 5. Test raw stdin / stdout pipeline
cat sample.txt | "${BIN_DIR}/lzfse" | "${BIN_DIR}/lzfse" -d > sample_pipe.txt
diff -u sample.txt sample_pipe.txt
echo "[PASS] Pipeline streaming (stdin -> lzfse -> lzfse -d -> stdout) verified"

# 6. Test Tar integration (create, list, extract)
python3 -c "
with open('data1.txt', 'w') as f:
    for i in range(2000):
        f.write(f'Line {i}: Apple LZFSE Tar multi-file archive test data.\n')
with open('data2.txt', 'w') as f:
    for i in range(1500):
        f.write(f'Record {i}: Finite State Entropy performance verification.\n')
"
ORIG_HASH1=$(sha256sum data1.txt | cut -d' ' -f1)
ORIG_HASH2=$(sha256sum data2.txt | cut -d' ' -f1)

tar --use-compress-program="${BIN_DIR}/lzfse" -cf archive.tar.lzfse data1.txt data2.txt
test -f archive.tar.lzfse
tar --use-compress-program="${BIN_DIR}/lzfse" -tf archive.tar.lzfse > tar_list.txt
grep "data1.txt" tar_list.txt > /dev/null
grep "data2.txt" tar_list.txt > /dev/null
echo "[PASS] Tar archive listing verified"

mkdir extracted_tar
tar --use-compress-program="${BIN_DIR}/lzfse" -xf archive.tar.lzfse -C extracted_tar
REST_HASH1=$(sha256sum extracted_tar/data1.txt | cut -d' ' -f1)
REST_HASH2=$(sha256sum extracted_tar/data2.txt | cut -d' ' -f1)

if [ "$ORIG_HASH1" != "$REST_HASH1" ] || [ "$ORIG_HASH2" != "$REST_HASH2" ]; then
    echo "[FAIL] SHA256 mismatch in Tar extraction!"
    exit 1
fi
echo "[PASS] Tar extraction SHA256 verified ($ORIG_HASH1, $ORIG_HASH2)"

echo "=== All LZFSE tests completed successfully! ==="
