# pea-c

A pure C implementation of the PEA archive format (version 1.6), the format
used by [PeaZip](https://github.com/peazip/PeaZip) and its `pea` command
line engine.

The on-disk layout and every control algorithm are derived from the Pascal
reference implementation shipped in `../peazip/peazip-sources/dev`
(`pea.pas`, `pea_utils.pas`). See `docs/FORMAT.md` for the byte-level notes
and `docs/PLAN.md` for the current scope.

## Current status

Implemented and tested:

* archive header, stream header, `POD`/`EOA` triggers
* store (`PCOMPRESS0`) and deflate (`PCOMPRESS1..3`) payloads
* object / stream / volume tags: `NOALGO`, `ADLER32`, `CRC32`, `CRC64`,
  `MD5`, `RIPEMD160`, `SHA1`, `SHA256`, `SHA512`, `SHA3_256`, `SHA3_512`,
  `WHIRLPOOL`, `BLAKE2S`, `BLAKE2B`
* encrypted streams: `EAX256` / `EAX` (AES EAX), `HMAC` (AES-128 CTR +
  HMAC-SHA1-128), `TF` / `TF256` (Twofish EAX), `SP` / `SP256` (Serpent EAX)
* cascade streams `TRIATS`..`HRISAT` (AES+Twofish+Serpent EAX-256, SHA3-384
  tag, PBKDF2 / scrypt / hybrid via `-n niter`)
* optional keyfile (first 2048 bytes appended to KDF material)
* multi-volume split (`-s volsize`): `name.00000N.pea`, per-volume tag
* creation, listing, verification and extraction

## Build

```sh
make
```

Requirements: a C11 compiler, zlib, OpenSSL and Nettle
(`-lz -lcrypto -lnettle`). EAX256 / TF256 / SP256 need the OpenSSL
legacy provider (Whirlpool). Twofish/Serpent use Nettle.

## Usage

```sh
# create, deflate level 6, SHA256 everywhere
./pea c -m PCOMPRESS2 -a SHA256 -o SHA256 -v SHA256 out.pea ./mydir

# EAX256 with password
./pea c -m PCOMPRESS2 -a EAX256 -p secret out.pea ./mydir

# HMAC with password and keyfile
./pea c -a HMAC -p secret -k ./key.bin out.pea ./mydir

# cascade AES+Twofish+Serpent (PBKDF2, niter 0)
./pea c -a TRIATS -p secret out.pea ./mydir

# split into 1 MiB volumes (writes out.000001.pea, out.000002.pea, ...)
./pea c -s 1048576 out.pea ./mydir

# list / verify / extract (pass the first volume for a split archive)
./pea l -p secret out.pea
./pea t out.000001.pea
./pea t -p secret -k ./key.bin out.pea
./pea x -p secret out.pea ./restore
```

`-m` accepts `PCOMPRESS0..3` or `0..3`; `-a` (stream), `-o` (object) and
`-v` (volume) accept an algorithm name or numeric code.

## Tests

```sh
make test
```

The suite round-trips every supported algorithm/compression combination
(including EAX / HMAC / TF / SP / cascades, keyfile and multi-volume split),
checks that a flipped payload byte is detected, and rejects non-PEA input.

Cross-check against the Pascal oracle:

```sh
DISPLAY=:99 ./tests/interop.sh
```

## Layout

```
src/pea.h            format constants and algorithm tables
src/pea_digest.[ch]  streaming control algorithms
src/pea_eax.[ch]     EAX-128/256, HMAC, TF/SP (Nettle)
src/pea_block.[ch]   generic OMAC1 + EAX over a 16-byte block cipher
src/pea_archive.[ch] writer and reader
src/pea_vol.[ch]     split-volume input stream
src/pea_cli.c        command line front-end
tests/run_tests.sh   round-trip tests
tests/interop.sh     C <-> Pascal oracle
docs/FORMAT.md       reverse-engineered format notes
docs/PLAN.md         phased plan and scope
```
