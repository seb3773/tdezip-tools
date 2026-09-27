#!/bin/bash
#
# Cross-check pea-c against the PeaZip Pascal oracle (project_pea).
# Requires: compiled ./pea, xvfb-run, and PASCAL_PEA (default:
# /workspace/peazip/peazip-sources/dev/pea).
#
set -u

cd "$(dirname "$0")/.."
C=$(pwd)/pea
PAS=${PASCAL_PEA:-/workspace/peazip/peazip-sources/dev/pea}
T=$(mktemp -d /tmp/pea-interop.XXXXXX)
PASS=0
FAIL=0

run_pas() {
    # Reuse a single X display (Xvfb started by the caller, or xvfb-run).
    # Bound each Pascal invocation: GTK/Lazarus can hang after many runs.
    if [ -n "${DISPLAY:-}" ]; then
        timeout 60 "$PAS" "$@" >/dev/null 2>&1
    else
        timeout 60 xvfb-run -a -s '-screen 0 640x480x24' "$PAS" "$@" >/dev/null 2>&1
    fi
}

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
    head -c 4096 /dev/urandom > "$d/rand.bin"
    head -c 200000 /dev/zero > "$d/zeros.bin"
}

# Flatten UnPEA output (files only, by basename) into a sorted listing
# of "relpath size sha256".
hash_tree() {
    local root="$1"
    (cd "$root" && find . -type f | sort | while IFS= read -r f; do
        sha256sum "$f"
    done)
}

# Compare extracted payload of two trees by filename (ignore parent path).
same_payloads() {
    local a="$1" b="$2"
    python3 - "$a" "$b" <<'PY'
import hashlib, os, sys
def files(root):
    out = {}
    for dirpath, _, names in os.walk(root):
        for n in names:
            p = os.path.join(dirpath, n)
            with open(p, 'rb') as f:
                out[n] = hashlib.sha256(f.read()).hexdigest()
    return out
A, B = files(sys.argv[1]), files(sys.argv[2])
missing = set(A) - set(B)
extra = set(B) - set(A)
mismatch = [k for k in A if k in B and A[k] != B[k]]
if missing or extra or mismatch:
    print('missing', missing)
    print('extra', extra)
    print('mismatch', mismatch)
    sys.exit(1)
PY
}

make_input "$T/in"

pas_create() {
    local outbase="$1" compr="$2" vol="$3" obj="$4" strm="$5"
    shift 5
    run_pas PEA "$outbase" 0 "$compr" "$vol" "$obj" "$strm" HIDDEN FROMCL "$@"
}

pas_extract() {
    local arc="$1" outd="$2"
    mkdir -p "$outd"
    run_pas UNPEA "$arc" "$outd/x" RESETDATE RESETATTR EXTRACT2DIR HIDDEN NOKEYFILE
}

pas_test() {
    local arc="$1" outd="$2"
    mkdir -p "$outd"
    run_pas UNPEA "$arc" "$outd/t" RESETDATE RESETATTR EXTRACT2TESTB HIDDEN NOKEYFILE
}

# --- cases: Pascal writer -> C reader ---
p2c() {
    local name="$1" compr="$2" vol="$3" obj="$4" strm="$5"
    local base="$T/$name"
    mkdir -p "$base"
    if ! pas_create "$base/arc" "$compr" "$vol" "$obj" "$strm" "$T/in"; then
        echo "FAIL  $name (pascal create)"
        FAIL=$((FAIL + 1))
        return
    fi
    local arc
    arc=$(ls "$base"/arc.pea "$base"/arc.000001.pea 2>/dev/null | head -1)
    if [ -z "$arc" ]; then
        echo "FAIL  $name (no pascal archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! "$C" t "$arc" >/dev/null 2>&1; then
        echo "FAIL  $name (c test pascal archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! "$C" l "$arc" >/dev/null 2>&1; then
        echo "FAIL  $name (c list pascal archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    mkdir -p "$base/cx"
    if ! "$C" x "$arc" "$base/cx" >/dev/null 2>&1; then
        echo "FAIL  $name (c extract pascal archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! same_payloads "$T/in" "$base/cx"; then
        echo "FAIL  $name (c extract content)"
        FAIL=$((FAIL + 1))
        return
    fi
    echo "PASS  $name"
    PASS=$((PASS + 1))
}

# --- cases: C writer -> Pascal reader ---
c2p() {
    local name="$1" compr="$2" vol="$3" obj="$4" strm="$5"
    local base="$T/$name"
    mkdir -p "$base"
    if ! "$C" c -m "$compr" -a "$strm" -o "$obj" -v "$vol" "$base/arc.pea" "$T/in" >/dev/null 2>&1; then
        echo "FAIL  $name (c create)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! pas_test "$base/arc.pea" "$base/pt"; then
        echo "FAIL  $name (pascal test c archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! pas_extract "$base/arc.pea" "$base/px"; then
        echo "FAIL  $name (pascal extract c archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! same_payloads "$T/in" "$base/px"; then
        echo "FAIL  $name (pascal extract content)"
        FAIL=$((FAIL + 1))
        return
    fi
    echo "PASS  $name"
    PASS=$((PASS + 1))
}

# --- byte-identical single-file store ---
ident() {
    local name=ident_store_crc32
    local base="$T/$name"
    mkdir -p "$base"
    local f="$T/in/a.txt"
    pas_create "$base/p" PCOMPRESS0 CRC32 CRC32 NOALGO "$f" || {
        echo "FAIL  $name (pascal create)"; FAIL=$((FAIL + 1)); return; }
    "$C" c -m PCOMPRESS0 -a NOALGO -o CRC32 -v CRC32 "$base/c.pea" "$f" >/dev/null 2>&1 || {
        echo "FAIL  $name (c create)"; FAIL=$((FAIL + 1)); return; }
    local parc
    parc=$(ls "$base"/p.pea 2>/dev/null | head -1)
    if cmp -s "$parc" "$base/c.pea"; then
        echo "PASS  $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL  $name (archives differ)"
        hexdump -C "$parc" | head -8
        hexdump -C "$base/c.pea" | head -8
        FAIL=$((FAIL + 1))
    fi
}

p2c p2c_store_crc     PCOMPRESS0 CRC32   CRC32   NOALGO
p2c p2c_store_sha256  PCOMPRESS0 SHA256  SHA256  SHA256
p2c p2c_store_adler   PCOMPRESS0 ADLER32 ADLER32 ADLER32
p2c p2c_p1_crc        PCOMPRESS1 CRC32   CRC32   CRC32
p2c p2c_p2_sha        PCOMPRESS2 SHA256  SHA256  SHA256
p2c p2c_p3_crc        PCOMPRESS3 CRC32   SHA256  CRC32

c2p c2p_store_crc     PCOMPRESS0 CRC32   CRC32   NOALGO
c2p c2p_store_sha256  PCOMPRESS0 SHA256  SHA256  SHA256
c2p c2p_p1_crc        PCOMPRESS1 CRC32   CRC32   CRC32
c2p c2p_p2_sha        PCOMPRESS2 SHA256  SHA256  SHA256
c2p c2p_p3_crc        PCOMPRESS3 CRC32   SHA256  CRC32

ident

# --- EAX256 interop (password required) ---
PW='test-password-pea'

pas_create_eax() {
    local outbase="$1" compr="$2" vol="$3" obj="$4"
    shift 4
    run_pas PEA "$outbase" 0 "$compr" "$vol" "$obj" EAX256 HIDDEN "$PW" NOKEYFILE FROMCL "$@"
}

pas_extract_eax() {
    local arc="$1" outd="$2"
    mkdir -p "$outd"
    run_pas UNPEA "$arc" "$outd/x" RESETDATE RESETATTR EXTRACT2DIR HIDDEN "$PW" NOKEYFILE
}

pas_test_eax() {
    local arc="$1" outd="$2"
    mkdir -p "$outd"
    run_pas UNPEA "$arc" "$outd/t" RESETDATE RESETATTR EXTRACT2TESTB HIDDEN "$PW" NOKEYFILE
}

p2c_eax() {
    local name="$1" compr="$2"
    local base="$T/$name"
    mkdir -p "$base"
    if ! pas_create_eax "$base/arc" "$compr" CRC32 CRC32 "$T/in"; then
        echo "FAIL  $name (pascal create)"
        FAIL=$((FAIL + 1))
        return
    fi
    local arc
    arc=$(ls "$base"/arc.pea "$base"/arc.000001.pea 2>/dev/null | head -1)
    if [ -z "$arc" ]; then
        echo "FAIL  $name (no pascal archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! "$C" t -p "$PW" "$arc" >/dev/null 2>&1; then
        echo "FAIL  $name (c test pascal archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    mkdir -p "$base/cx"
    if ! "$C" x -p "$PW" "$arc" "$base/cx" >/dev/null 2>&1; then
        echo "FAIL  $name (c extract pascal archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! same_payloads "$T/in" "$base/cx"; then
        echo "FAIL  $name (c extract content)"
        FAIL=$((FAIL + 1))
        return
    fi
    echo "PASS  $name"
    PASS=$((PASS + 1))
}

c2p_eax() {
    local name="$1" compr="$2"
    local base="$T/$name"
    mkdir -p "$base"
    if ! "$C" c -m "$compr" -a EAX256 -o CRC32 -v CRC32 -p "$PW" "$base/arc.pea" "$T/in" >/dev/null 2>&1; then
        echo "FAIL  $name (c create)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! pas_test_eax "$base/arc.pea" "$base/pt"; then
        echo "FAIL  $name (pascal test c archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! pas_extract_eax "$base/arc.pea" "$base/px"; then
        echo "FAIL  $name (pascal extract c archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! same_payloads "$T/in" "$base/px"; then
        echo "FAIL  $name (pascal extract content)"
        FAIL=$((FAIL + 1))
        return
    fi
    echo "PASS  $name"
    PASS=$((PASS + 1))
}

p2c_eax p2c_eax_store PCOMPRESS0
p2c_eax p2c_eax_p2    PCOMPRESS2
c2p_eax c2p_eax_store PCOMPRESS0
c2p_eax c2p_eax_p2    PCOMPRESS2

# --- HMAC / EAX-128 / keyfile interop ---
KF="$T/key.bin"
printf 'keyfile-bytes-for-pea\n' > "$KF"

pas_create_crypto() {
    local outbase="$1" compr="$2" algo="$3" keyf="$4"
    shift 4
    run_pas PEA "$outbase" 0 "$compr" CRC32 CRC32 "$algo" HIDDEN "$PW" "$keyf" FROMCL "$@"
}

pas_extract_crypto() {
    local arc="$1" outd="$2" keyf="$3"
    mkdir -p "$outd"
    run_pas UNPEA "$arc" "$outd/x" RESETDATE RESETATTR EXTRACT2DIR HIDDEN "$PW" "$keyf"
}

pas_test_crypto() {
    local arc="$1" outd="$2" keyf="$3"
    mkdir -p "$outd"
    run_pas UNPEA "$arc" "$outd/t" RESETDATE RESETATTR EXTRACT2TESTB HIDDEN "$PW" "$keyf"
}

p2c_crypto() {
    local name="$1" compr="$2" algo="$3" keyf="$4"
    local extra=()
    local base="$T/$name"
    mkdir -p "$base"
    if [ "$keyf" != NOKEYFILE ]; then
        extra=(-k "$keyf")
    fi
    if ! pas_create_crypto "$base/arc" "$compr" "$algo" "$keyf" "$T/in"; then
        echo "FAIL  $name (pascal create)"
        FAIL=$((FAIL + 1))
        return
    fi
    local arc
    arc=$(ls "$base"/arc.pea "$base"/arc.000001.pea 2>/dev/null | head -1)
    if [ -z "$arc" ]; then
        echo "FAIL  $name (no pascal archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! "$C" t -p "$PW" "${extra[@]}" "$arc" >/dev/null 2>&1; then
        echo "FAIL  $name (c test pascal archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    mkdir -p "$base/cx"
    if ! "$C" x -p "$PW" "${extra[@]}" "$arc" "$base/cx" >/dev/null 2>&1; then
        echo "FAIL  $name (c extract pascal archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! same_payloads "$T/in" "$base/cx"; then
        echo "FAIL  $name (c extract content)"
        FAIL=$((FAIL + 1))
        return
    fi
    echo "PASS  $name"
    PASS=$((PASS + 1))
}

c2p_crypto() {
    local name="$1" compr="$2" algo="$3" keyf="$4"
    local extra=()
    local base="$T/$name"
    mkdir -p "$base"
    if [ "$keyf" != NOKEYFILE ]; then
        extra=(-k "$keyf")
    fi
    if ! "$C" c -m "$compr" -a "$algo" -o CRC32 -v CRC32 -p "$PW" "${extra[@]}" "$base/arc.pea" "$T/in" >/dev/null 2>&1; then
        echo "FAIL  $name (c create)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! pas_test_crypto "$base/arc.pea" "$base/pt" "$keyf"; then
        echo "FAIL  $name (pascal test c archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! pas_extract_crypto "$base/arc.pea" "$base/px" "$keyf"; then
        echo "FAIL  $name (pascal extract c archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! same_payloads "$T/in" "$base/px"; then
        echo "FAIL  $name (pascal extract content)"
        FAIL=$((FAIL + 1))
        return
    fi
    echo "PASS  $name"
    PASS=$((PASS + 1))
}

p2c_crypto p2c_hmac_store PCOMPRESS0 HMAC NOKEYFILE
p2c_crypto p2c_eax128_p2  PCOMPRESS2 EAX  NOKEYFILE
p2c_crypto p2c_eax256_kf  PCOMPRESS0 EAX256 "$KF"
c2p_crypto c2p_hmac_store PCOMPRESS0 HMAC NOKEYFILE
c2p_crypto c2p_eax128_p2  PCOMPRESS2 EAX  NOKEYFILE
c2p_crypto c2p_hmac_kf    PCOMPRESS2 HMAC "$KF"

# --- multi-volume (isolated: one Pascal invocation each way) ---
p2c_split() {
    local name=p2c_split_store
    local base="$T/$name"
    mkdir -p "$base"
    if ! run_pas PEA "$base/arc" 65536 PCOMPRESS0 CRC32 CRC32 NOALGO HIDDEN FROMCL "$T/in"; then
        echo "FAIL  $name (pascal create)"
        FAIL=$((FAIL + 1))
        return
    fi
    local arc="$base/arc.000001.pea"
    if [ ! -f "$arc" ]; then
        echo "FAIL  $name (no pascal split archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! "$C" t "$arc" >/dev/null 2>&1; then
        echo "FAIL  $name (c test pascal archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    mkdir -p "$base/cx"
    if ! "$C" x "$arc" "$base/cx" >/dev/null 2>&1; then
        echo "FAIL  $name (c extract pascal archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! same_payloads "$T/in" "$base/cx"; then
        echo "FAIL  $name (c extract content)"
        FAIL=$((FAIL + 1))
        return
    fi
    echo "PASS  $name"
    PASS=$((PASS + 1))
}

c2p_split() {
    local name=c2p_split_store
    local base="$T/$name"
    mkdir -p "$base"
    if ! "$C" c -m PCOMPRESS0 -a NOALGO -o CRC32 -v CRC32 -s 65536 "$base/arc.pea" "$T/in" >/dev/null 2>&1; then
        echo "FAIL  $name (c create)"
        FAIL=$((FAIL + 1))
        return
    fi
    local first="$base/arc.000001.pea"
    if [ ! -f "$first" ]; then
        echo "FAIL  $name (missing first volume)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! pas_test "$first" "$base/pt"; then
        echo "FAIL  $name (pascal test c archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! pas_extract "$first" "$base/px"; then
        echo "FAIL  $name (pascal extract c archive)"
        FAIL=$((FAIL + 1))
        return
    fi
    if ! same_payloads "$T/in" "$base/px"; then
        echo "FAIL  $name (pascal extract content)"
        FAIL=$((FAIL + 1))
        return
    fi
    echo "PASS  $name"
    PASS=$((PASS + 1))
}

p2c_split
c2p_split

p2c_crypto p2c_tf_store   PCOMPRESS0 TF    NOKEYFILE
p2c_crypto p2c_sp256_p2   PCOMPRESS2 SP256 NOKEYFILE
c2p_crypto c2p_tf256_store PCOMPRESS0 TF256 NOKEYFILE
c2p_crypto c2p_sp_p2      PCOMPRESS2 SP    NOKEYFILE

p2c_crypto p2c_triats_store PCOMPRESS0 TRIATS NOKEYFILE
c2p_crypto c2p_triats_store PCOMPRESS0 TRIATS NOKEYFILE
p2c_crypto p2c_tritsa_p2    PCOMPRESS2 TRITSA NOKEYFILE

echo
echo "interop: $PASS passed, $FAIL failed"
echo "artifacts: $T"
[ "$FAIL" -eq 0 ]
