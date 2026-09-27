#!/bin/bash
#
# Round-trip tests for the pure C PEA implementation.
# Focus: Phase 1 (store), Phase 2 (tags), Phase 3 (deflate).
#
set -u

cd "$(dirname "$0")/.."
PEA=./pea
T=$(mktemp -d /tmp/pea-c-tests.XXXXXX)
PASS=0
FAIL=0

check() {
    local name="$1"
    shift
    if "$@"; then
        echo "PASS  $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL  $name"
        FAIL=$((FAIL + 1))
    fi
}

make_input() {
    local d="$1"
    mkdir -p "$d/sub/deep"
    printf 'hello pea world\n' > "$d/a.txt"
    printf 'nested\n' > "$d/sub/n.txt"
    printf 'deep\n' > "$d/sub/deep/d.txt"
    : > "$d/empty"
    head -c 200000 /dev/urandom > "$d/rand.bin"
    head -c 1048576 /dev/zero > "$d/sub/deep/exact.bin"
}

run_case() {
    local name="$1" method="$2" stream="$3" obj="$4" vol="$5"
    local arc="$T/$name.pea"
    local out="$T/$name.out"

    "$PEA" c -m "$method" -a "$stream" -o "$obj" -v "$vol" "$arc" "$T/in" \
        > /dev/null 2>&1 || { echo "FAIL  $name (create)"; FAIL=$((FAIL + 1)); return; }
    "$PEA" t "$arc" > /dev/null 2>&1 || { echo "FAIL  $name (tags)";   FAIL=$((FAIL + 1)); return; }
    "$PEA" l "$arc" > /dev/null 2>&1 || { echo "FAIL  $name (list)";   FAIL=$((FAIL + 1)); return; }
    mkdir -p "$out"
    "$PEA" x "$arc" "$out" > /dev/null 2>&1 || { echo "FAIL  $name (extract)"; FAIL=$((FAIL + 1)); return; }
    diff -r "$T/in" "$out/in" > /dev/null 2>&1 \
        || { echo "FAIL  $name (content)"; FAIL=$((FAIL + 1)); return; }
    echo "PASS  $name"
    PASS=$((PASS + 1))
}

make_input "$T/in"

run_case p1_store_noalgo   PCOMPRESS0 NOALGO  NOALGO  NOALGO
run_case p1_store_crc32    PCOMPRESS0 CRC32   CRC32   CRC32
run_case p2_obj_sha256     PCOMPRESS0 CRC32   SHA256  CRC32
run_case p2_all_sha256     PCOMPRESS0 SHA256  SHA256  SHA256
run_case p2_all_sha512     PCOMPRESS0 SHA512  SHA512  SHA512
run_case p2_all_adler32    PCOMPRESS0 ADLER32 ADLER32 ADLER32
run_case p2_all_crc64      PCOMPRESS0 CRC64   CRC64   CRC64
run_case p2_all_md5        PCOMPRESS0 MD5     MD5     MD5
run_case p2_all_ripemd160  PCOMPRESS0 RIPEMD160 RIPEMD160 RIPEMD160
run_case p2_all_whirlpool  PCOMPRESS0 WHIRLPOOL WHIRLPOOL WHIRLPOOL
run_case p2_all_sha3_256   PCOMPRESS0 SHA3_256 SHA3_256 SHA3_256
run_case p2_all_sha3_512   PCOMPRESS0 SHA3_512 SHA3_512 SHA3_512
run_case p2_all_blake2s    PCOMPRESS0 BLAKE2S BLAKE2S BLAKE2S
run_case p2_all_blake2b    PCOMPRESS0 BLAKE2B BLAKE2B BLAKE2B
run_case p3_p1             PCOMPRESS1 SHA256  SHA256  SHA256
run_case p3_p2             PCOMPRESS2 CRC32   SHA256  CRC32
run_case p3_p3             PCOMPRESS3 SHA256  SHA256  SHA256

# Phase 4+: EAX256 / EAX-128 / HMAC round-trip, optional keyfile.
run_crypto() {
    local name="$1" method="$2" algo="$3"
    local extra=()
    shift 3
    extra=("$@")
    local arc="$T/$name.pea"
    local out="$T/$name.out"
    local pw='test-password-pea'

    "$PEA" c -m "$method" -a "$algo" -o CRC32 -v CRC32 -p "$pw" "${extra[@]}" "$arc" "$T/in" \
        > /dev/null 2>&1 || { echo "FAIL  $name (create)"; FAIL=$((FAIL + 1)); return; }
    "$PEA" t -p "$pw" "${extra[@]}" "$arc" > /dev/null 2>&1 \
        || { echo "FAIL  $name (tags)"; FAIL=$((FAIL + 1)); return; }
    if "$PEA" t -p 'wrong-password' "${extra[@]}" "$arc" > /dev/null 2>&1; then
        echo "FAIL  $name (wrong password accepted)"
        FAIL=$((FAIL + 1))
        return
    fi
    mkdir -p "$out"
    "$PEA" x -p "$pw" "${extra[@]}" "$arc" "$out" > /dev/null 2>&1 \
        || { echo "FAIL  $name (extract)"; FAIL=$((FAIL + 1)); return; }
    diff -r "$T/in" "$out/in" > /dev/null 2>&1 \
        || { echo "FAIL  $name (content)"; FAIL=$((FAIL + 1)); return; }
    echo "PASS  $name"
    PASS=$((PASS + 1))
}

printf 'keyfile-bytes-for-pea\n' > "$T/key.bin"
head -c 3000 /dev/urandom > "$T/key.long"

run_crypto p4_eax256_store PCOMPRESS0 EAX256
run_crypto p4_eax256_p2    PCOMPRESS2 EAX256
run_crypto p4_eax_store    PCOMPRESS0 EAX
run_crypto p4_hmac_store   PCOMPRESS0 HMAC
run_crypto p4_hmac_p2      PCOMPRESS2 HMAC
run_crypto p4_eax256_kf    PCOMPRESS0 EAX256 -k "$T/key.bin"
run_crypto p4_hmac_kf      PCOMPRESS2 HMAC   -k "$T/key.long"
run_crypto p4_tf_store     PCOMPRESS0 TF
run_crypto p4_tf256_p2     PCOMPRESS2 TF256
run_crypto p4_sp_store     PCOMPRESS0 SP
run_crypto p4_sp256_p2     PCOMPRESS2 SP256
run_crypto p4_tf256_kf     PCOMPRESS0 TF256 -k "$T/key.bin"
run_crypto p6_triats_store PCOMPRESS0 TRIATS
run_crypto p6_tritsa_p2    PCOMPRESS2 TRITSA
run_crypto p6_trisat_store PCOMPRESS0 TRISAT
run_crypto p6_sriats_store PCOMPRESS0 SRIATS
run_crypto p6_hriats_store PCOMPRESS0 HRIATS
run_crypto p6_triats_kf    PCOMPRESS0 TRIATS -k "$T/key.bin"

# Keyfile mismatch must fail.
if "$PEA" t -p 'test-password-pea' -k "$T/key.bin" "$T/p4_eax256_store.pea" > /dev/null 2>&1; then
    echo "FAIL  keyfile-mismatch"
    FAIL=$((FAIL + 1))
else
    echo "PASS  keyfile-mismatch"
    PASS=$((PASS + 1))
fi

# Corruption detection.
cp "$T/p2_all_sha256.pea" "$T/corrupt.pea"
printf '\xff' | dd of="$T/corrupt.pea" bs=1 seek=100000 count=1 conv=notrunc status=none
if "$PEA" t "$T/corrupt.pea" > /dev/null 2>&1; then
    echo "FAIL  corruption-detection"
    FAIL=$((FAIL + 1))
else
    echo "PASS  corruption-detection"
    PASS=$((PASS + 1))
fi

# Multi-volume: writer emits name.00000N.pea; reader stitches + checks each tag.
run_split() {
    local name="$1" method="$2" stream="$3" obj="$4" vol="$5" vsize="$6"
    local extra=()
    shift 6
    extra=("$@")
    local arc="$T/$name.pea"
    local first="$T/$name.000001.pea"
    local out="$T/$name.out"

    "$PEA" c -m "$method" -a "$stream" -o "$obj" -v "$vol" -s "$vsize" "${extra[@]}" \
        "$arc" "$T/in" > /dev/null 2>&1 \
        || { echo "FAIL  $name (create)"; FAIL=$((FAIL + 1)); return; }
    [ -f "$first" ] || { echo "FAIL  $name (missing $first)"; FAIL=$((FAIL + 1)); return; }
    local nvol
    nvol=$(ls "$T/$name".[0-9][0-9][0-9][0-9][0-9][0-9].pea 2>/dev/null | wc -l)
    [ "$nvol" -ge 2 ] || { echo "FAIL  $name (expected >=2 volumes, got $nvol)"; FAIL=$((FAIL + 1)); return; }
    "$PEA" t "${extra[@]}" "$first" > /dev/null 2>&1 \
        || { echo "FAIL  $name (tags)"; FAIL=$((FAIL + 1)); return; }
    "$PEA" l "${extra[@]}" "$first" > /dev/null 2>&1 \
        || { echo "FAIL  $name (list)"; FAIL=$((FAIL + 1)); return; }
    mkdir -p "$out"
    "$PEA" x "${extra[@]}" "$first" "$out" > /dev/null 2>&1 \
        || { echo "FAIL  $name (extract)"; FAIL=$((FAIL + 1)); return; }
    diff -r "$T/in" "$out/in" > /dev/null 2>&1 \
        || { echo "FAIL  $name (content)"; FAIL=$((FAIL + 1)); return; }
    echo "PASS  $name ($nvol volumes)"
    PASS=$((PASS + 1))
}

run_split p5_split_store   PCOMPRESS0 CRC32 CRC32 CRC32 4096
run_split p5_split_p2      PCOMPRESS2 SHA256 SHA256 CRC32 8192
run_split p5_split_noalgo  PCOMPRESS0 NOALGO NOALGO NOALGO 2048
run_split p5_split_eax256  PCOMPRESS0 EAX256 CRC32 CRC32 4096 -p 'test-password-pea'
run_split p5_split_hmac    PCOMPRESS2 HMAC CRC32 SHA256 3072 -p 'test-password-pea' -k "$T/key.bin"

# Intermediate volume size (except last) equals -s.
sz1=$(stat -c %s "$T/p5_split_store.000001.pea")
if [ "$sz1" -eq 4096 ]; then
    echo "PASS  p5_split_volsize"
    PASS=$((PASS + 1))
else
    echo "FAIL  p5_split_volsize (first volume $sz1, expected 4096)"
    FAIL=$((FAIL + 1))
fi

# Non zero exit for a non-PEA file.
printf 'not a pea archive' > "$T/bogus.bin"
if "$PEA" l "$T/bogus.bin" > /dev/null 2>&1; then
    echo "FAIL  reject-non-pea"
    FAIL=$((FAIL + 1))
else
    echo "PASS  reject-non-pea"
    PASS=$((PASS + 1))
fi

echo
echo "tests: $PASS passed, $FAIL failed"
echo "artifacts kept under $T"
[ "$FAIL" -eq 0 ]
