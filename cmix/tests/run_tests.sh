#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${SCRIPT_DIR}/../cmix"
TEST_DIR="${SCRIPT_DIR}/tmp_test"

echo "=== Running CMIX Test Suite ==="

# 1. Check version & help
"$BIN" -v | grep -q "cmix version 21"
echo "  [PASS] cmix -v"
"$BIN" -h | grep -q "Usage:"
echo "  [PASS] cmix -h"

# Prepare test directory
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

# 2. Basic compression & decompression
echo "This is a context mixing test sample for Byron Knoll's cmix version 21 compression test." > "$TEST_DIR/sample.txt"
"$BIN" -c "$TEST_DIR/sample.txt" "$TEST_DIR/sample.cmix"
"$BIN" -d "$TEST_DIR/sample.cmix" "$TEST_DIR/sample.out"
diff -q "$TEST_DIR/sample.txt" "$TEST_DIR/sample.out"
echo "  [PASS] file compress / decompress cycle"

# 3. Archive integrity test
"$BIN" -t "$TEST_DIR/sample.cmix"
echo "  [PASS] archive integrity test (-t)"

# 4. Pipe mode test (stdin / stdout for GNU Tar)
cat "$TEST_DIR/sample.txt" | "$BIN" -c | "$BIN" -d > "$TEST_DIR/sample_pipe.out"
diff -q "$TEST_DIR/sample.txt" "$TEST_DIR/sample_pipe.out"
echo "  [PASS] stream pipe compress / decompress cycle"

# 5. No preprocessing mode (-n)
"$BIN" -n "$TEST_DIR/sample.txt" "$TEST_DIR/sample_nopre.cmix"
"$BIN" -d "$TEST_DIR/sample_nopre.cmix" "$TEST_DIR/sample_nopre.out"
diff -q "$TEST_DIR/sample.txt" "$TEST_DIR/sample_nopre.out"
echo "  [PASS] no preprocessing mode (-n)"

# Cleanup
rm -rf "$TEST_DIR"
echo "=== ALL CMIX TESTS PASSED SUCCESSFULLY ==="
