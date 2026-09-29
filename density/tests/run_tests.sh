#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${SCRIPT_DIR}/../density"
TEST_DIR="${SCRIPT_DIR}/sandbox"

rm -rf "${TEST_DIR}"
mkdir -p "${TEST_DIR}"
cd "${TEST_DIR}"

echo "=== Running density test suite ==="

# 1. Version and Help
"${BIN}" -V > /dev/null
"${BIN}" -h 2> /dev/null || true

# 2. Text payload roundtrip with all 3 algorithms
python3 -c '
with open("test_sample.txt", "wb") as f:
    for i in range(10000):
        f.write(f"Record {i}: Chameleon, Cheetah, Lion high throughput compression benchmark\n".encode())
'

for algo in 1 2 3; do
    echo "Testing algorithm -${algo}..."
    "${BIN}" -${algo} -v -i test_sample.txt -o "sample_${algo}.density"
    "${BIN}" -t "sample_${algo}.density"
    "${BIN}" -d -i "sample_${algo}.density" -o "sample_${algo}.restored"
    cmp test_sample.txt "sample_${algo}.restored"
done

# 3. Pipeline streaming stdin -> stdout
cat test_sample.txt | "${BIN}" -2 -c | "${BIN}" -d -c > restored_pipe.txt
cmp test_sample.txt restored_pipe.txt

# 4. GNU Tar integration
mkdir tar_content
cp test_sample.txt tar_content/data.txt
tar --use-compress-program="${BIN} -2" -cf test_archive.tar.density tar_content
tar --use-compress-program="${BIN}" -tf test_archive.tar.density > /dev/null
mkdir tar_extracted
tar --use-compress-program="${BIN}" -xf test_archive.tar.density -C tar_extracted
cmp tar_content/data.txt tar_extracted/tar_content/data.txt

# 5. Clean up
cd "${SCRIPT_DIR}"
rm -rf "${TEST_DIR}"

echo "=== All density tests passed successfully ==="
