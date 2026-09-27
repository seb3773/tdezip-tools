# PEA file format 1.6 - reverse-engineering notes

Source of truth: the Pascal reference implementation in
`peazip/peazip-sources/dev/pea_utils.pas` (header/encoding) and `pea.pas`
(reader/writer). PDF spec: `pea_help.pdf` at the workspace root.

All integers are little-endian. Strings inside the format are ANSI byte
strings (this implementation stores the raw bytes; ASCII paths round-trip).

## Archive header (10 bytes)

| offset | size | field |
|--------|------|-------|
| 0 | 1 | magic `0xEA` |
| 1 | 1 | format version (`1`) |
| 2 | 1 | format revision (`6`) |
| 3 | 1 | volume control algorithm code |
| 4 | 1 | archive-wide ECC scheme (always `0`) |
| 5 | 1 | OS code (`0x33` = Linux) |
| 6 | 1 | system date-time encoding (`0x30` = Unix) |
| 7 | 1 | character encoding (`0x01` = ANSI) |
| 8 | 1 | CPU type, endianness in MSB (`0x21` = x86_64 LE) |
| 9 | 1 | KDF iteration multiplier (`niter`, encryption only) |

## Stream header (10 bytes)

| offset | size | field |
|--------|------|-------|
| 0..1 | 2 | `00 00` |
| 2..5 | 4 | ASCII `POD\0` (start of stream / "pea pod") |
| 6 | 1 | compression code |
| 7 | 1 | stream-wide ECC scheme (always `0`) |
| 8 | 1 | stream control algorithm code |
| 9 | 1 | object control algorithm code |

## Control algorithm codes

| code | algorithm | tag size |
|------|-----------|----------|
| 0x00 | NOALGO | 0 |
| 0x01 | ADLER32 | 4 |
| 0x02 | CRC32 (reflected, poly `0xEDB88320`) | 4 |
| 0x03 | CRC64 | 8 |
| 0x10 | MD5 | 16 |
| 0x11 | RIPEMD160 | 20 |
| 0x12 | SHA1 | 20 |
| 0x13 | SHA256 | 32 |
| 0x14 | SHA512 | 64 |
| 0x15 | WHIRLPOOL | 64 |
| 0x16 | SHA3_256 | 32 |
| 0x17 | SHA3_512 | 64 |
| 0x18 | BLAKE2S | 32 |
| 0x19 | BLAKE2B | 64 |
| 0x30 | HMAC (password) | 16 |
| 0x31 | EAX (AES-128 EAX) | 16 |
| 0x32 | TF (Twofish EAX) | 16 |
| 0x33 | SP (Serpent EAX) | 16 |
| 0x41 | EAX256 (AES-256 EAX) | 16 |
| 0x42 | TF256 | 16 |
| 0x43 | SP256 | 16 |
| 0x44..0x4C | AES+Twofish+Serpent cascades | 48 (SHA3-384) |

Only the non-password algorithms (0x00..0x19) are valid for the object
control field and for the volume control field.

## Compression codes

| code | name | deflate level | WBUFSIZE field |
|------|------|---------------|----------------|
| 0 | PCOMPRESS0 | none (store) | absent |
| 1 | PCOMPRESS1 | 3 | present |
| 2 | PCOMPRESS2 | 6 | present |
| 3 | PCOMPRESS3 | 9 | present |

PCOMPRESS1..3 are independent deflate blocks (zlib wrapped), so a block can
be decompressed without the preceding ones. The stream header is followed by
a 4-byte `WBUFSIZE` (1048576) when compression is enabled.

## Crypto subheader (16 bytes, password algorithms only)

| offset | size | field |
|--------|------|-------|
| 0 | 1 | FCAsig (written as `0`) |
| 1 | 1 | Flags (written as `0`) |
| 2..13 | 12 | salt: 3 x u32 LE (`hdr.Salt[0..2]`) |
| 14..15 | 2 | password verifier (`PW_Ver`) |

For EAX256 the salt is the first 12 bytes of `SHA1(salt512)` generated from
system entropy/fingerprint; the reader just reads it from the header.

## Objects

Each object starts with a 2-byte name length.

* `name_len == 0` -> trigger object: the next 4 bytes are `EOS\0` (end of
  stream, unused in 1.0) or `EOA\0` (end of archive).
* `name_len > 0` -> directory or file:
  * 2 bytes name length
  * name (ANSI, absolute canonical path of `name_len` bytes)
  * 4 bytes last-modification time
  * 4 bytes attributes (FPC `filegetattr`; `0x10` = directory)
  * only for files: 8 bytes original size
  * only for non-empty files: payload (see below)
  * object tag (its size is the object algorithm tag size)

Payload, store (`PCOMPRESS0`): the raw bytes.

Payload, compressed: a sequence of blocks
`[u32 compsize][compsize bytes]` followed, once per file, by a trailing
`u32` holding the uncompressed size of the last block. A block is stored
verbatim when `compsize >= uncompressed_len` (writer fallback for
incompressible data), otherwise it is a zlib stream.

## Volumes

A single-volume archive is one file named `name.pea`.

A split archive is a sequence `name.000001.pea`, `name.000002.pea`, ...
(6-digit decimal index). UnPEA treats the input as split iff the given
path ends with `.000001.pea` (case-insensitive).

`volsize` is the on-disk size of each volume **including** the volume tag.
The writer stores `ch_size = max(volsize, volume_authsize+10) - volume_authsize`
payload bytes per volume, then appends the volume tag (not covered by the
volume hash). The last volume may be shorter. The first volume is always
at least 10 payload bytes so UnPEA can read the archive header before
knowing `volume_authsize`.

Reader: after consuming `filesize - volume_authsize` payload bytes, verify
the tag, reset the volume hash, and open the next numbered file.

## Trailer

1. EOA trigger (6 bytes).
2. Stream tag (size of the stream algorithm's tag).
3. Volume tag (size of the volume algorithm's tag) at the end of **each** volume.

## Control scopes

* **Volume tag** covers every byte physically written to the volume file,
  including the stream tag. The volume tag itself is not covered.
* **Stream tag** covers, in order: archive header + stream header (20 bytes,
  non-encrypted variants only), the optional `WBUFSIZE` field, every object
  header, the stored payload bytes, every object tag and the EOA trigger.
* **Object tag** covers the object header followed by the *uncompressed*
  data (for compressed payloads the compressed size field is included, the
  stored bytes are not).

## Phase 4 (encryption) parameters — EAX256 (`FCA_EAX256_init`)

* Crypto subheader (16 bytes, plaintext, not covered by the stream tag):
  `00 00 | salt[12] | PW_Ver[2]`. Salt is 12 random bytes (Pascal: first 12
  bytes of `SHA1(salt512)`); `PW_Ver` is little-endian.
* Key material for PBKDF2:
  `password || archive_header(10) || stream_header(10) || stream_header[0..1]`
  (22 extra bytes; the last two are always `00 00`). Optional keyfile: up to
  2048 bytes appended after that (`use_keyfile` in `pea_utils.pas`).
* `XKey` = 66 bytes from PBKDF2-HMAC-Whirlpool, 1000 iterations (`KeyIterations`
  in `fcaes256.pas`), salt = the 12-byte subheader salt:
  `ak[32]` AES-256 key, `hk[32]` EAX nonce, `pv[2]` password verifier.
  Reject the archive if `pv` does not match `PW_Ver`.
   (`FCA_EAX256_initP` / scrypt / `niter` is used by cascade algorithms.)
* Cipher: AES-256 EAX (Ehrhardt): NonceTag = OMAC(0||hk), CTR starts at
  NonceTag with IncMSBFull, MsgOMAC n[15]=2, HdrOMAC n[15]=1 (empty header).
  Encrypt then OMAC ciphertext; decrypt OMAC then CTR. Stream tag = 16 bytes
  `MsgOMAC xor HdrOMAC xor NonceTag`, written in the clear (volume-scoped).
* Stream scope under password algorithms starts after the crypto subheader
  (WBUFSIZE, objects, object tags, EOA). Archive and stream headers are not
  authenticated by EAX/HMAC.
* On-disk subheader bytes 0..1 are written as `00 00` (Pascal `pea_*_subhdr`
  zeros FCAsig/Flags); the reader ignores them and uses salt + PW_Ver only.

## HMAC (`FCA_HMAC_init`) and EAX-128 (`FCA_EAX_init`)

Same 16-byte subheader and the same password/keyfile material as EAX256.

* PBKDF2-HMAC-SHA1, 1000 iterations, `XKey` = 34 bytes:
  `ak[16]` AES-128 key, `hk[16]` HMAC key or EAX nonce, `pv[2]`.
* HMAC: AES-128 CTR, initial CTR = 0, IncMSBFull; HMAC-SHA1 over ciphertext,
  tag = first 16 bytes of the HMAC. Encrypt then HMAC; decrypt HMAC then CTR.
* EAX (code 49): same Ehrhardt EAX construction as EAX256, but AES-128 /
  16-byte nonce.
## Twofish (`TF` / `TF256`) and Serpent (`SP` / `SP256`)

Same 16-byte subheader, password/keyfile material, EAX construction and
IncMSBFull CTR as AES EAX. Cipher is Twofish or Serpent (Nettle) instead
of AES. 128-bit modes use PBKDF2-SHA1 / XKey 34; 256-bit modes use
PBKDF2-Whirlpool / XKey 66.

## Cascades (`TRIATS`..`HRISAT`, codes 0x44..0x4C)

Three 16-byte EAX-256 subheaders (AES, Twofish, Serpent; order from the
name). Password material is the same 22-byte header suffix [+keyfile].
Layer 0 uses the password as-is; layer 1 xors each byte with
`(pw_len + i) xor upcase(name[len-2])`; layer 2 with
`(pw_len xor i) xor upcase(name[len-1])`.

KDF is `FCA_*_initP` (not the 1000-iter EAX256 path):

* `TR*` = PBKDF2: AES Whirlpool `(niter*100000)+25000`, TF SHA-512
  `+50000`, SP SHA3-512 `+75000`
* `SR*` = scrypt `N=memiter, r=8, p=piter` on all three
* `HR*` hybrid: AES scrypt `(N=mem, r=8)`, TF scrypt `(N=mem/2, r=16)`,
  SP PBKDF2-SHA3-512 (hybrid iteration table)

`memiter` is 64 KiB for `niter=0`, then 128/256/512/1024 KiB; `piter` is
1 except niter 5/6/7 = 2/4/8. `niter` is archive header byte 9.

On disk the first two `PW_Ver` fields are zeroed; the third is
`pv0 xor pv1 xor pv2`. After the 48-byte subheader the writer encrypts
1..128 random bytes (`len-1` in the first plaintext byte) to hide the
exact archive size. Stream tag is SHA3-384 of the three 16-byte EAX tags
(order = encrypt order). Decrypt reverses the three EAX layers.
