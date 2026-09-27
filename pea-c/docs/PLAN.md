# Plan: pure C implementation of the PEA format

Goal: a C99 library + CLI (`pea`) that reads and writes PEA 1.x archives
compatible with PeaZip, without any Pascal dependency. The Pascal sources
are kept as the normative oracle for the on-disk layout and algorithms.

## Frozen scope: phases 1-4 as the first milestone

| phase | content | status |
|-------|---------|--------|
| 0 | project skeleton, format notes, build, test harness | done |
| 1 | header + POD/EOA triggers + PCOMPRESS0 + NOALGO | done |
| 2 | object/stream/volume tags (ADLER32, CRC32, SHA1, SHA256, SHA512) | done |
| 3 | PCOMPRESS1..3 (zlib deflate, independent blocks) | done |
| 4 | EAX-AES-256 + PBKDF2 (Whirlpool), `EAX256` streams | done |

Follow-up (this tree): HMAC, EAX-128, optional keyfile (2048 bytes),
remaining hashes (CRC64, MD5, RIPEMD160, WHIRLPOOL, SHA3, BLAKE2),
multi-volume split (`-s`), Twofish/Serpent, cascades (`TRIATS`..`HRISAT`).

Out of scope: PEA 2.0 (unfinished), GUI, WIPE / SANITIZE / MOTW helpers.

## Deliverables

* `src/pea.h` - format constants and algorithm tables.
* `src/pea_digest.[ch]` - streaming control algorithms.
* `src/pea_eax.[ch]` - EAX-128/256, HMAC, TF/SP, cascades.
* `src/pea_block.[ch]` - generic OMAC1 + EAX (Twofish/Serpent via Nettle).
* `src/pea_archive.[ch]` - writer and reader.
* `src/pea_vol.[ch]` - split-volume input stream.
* `src/pea_cli.c` - `pea c|l|t|x`.
* `tests/run_tests.sh` - round-trip and corruption tests.
* `tests/interop.sh` - C <-> Pascal oracle.

## Validation strategy

* Self round-trip for every supported algorithm/compression combination.
* Corruption detection (single flipped byte must fail tag verification).
* Interop with the Pascal binary: build `project_pea.lpi` with Lazarus and
  check (a) Pascal archives read by our CLI, (b) our archives listed and
  extracted by the Pascal `pea`.

## CLI

```
pea c [-m method] [-a stream] [-o object] [-v volume] [-s volsize] [-p password] [-k keyfile] archive path...
pea l [-p password] [-k keyfile] archive
pea t [-p password] [-k keyfile] archive
pea x [-p password] [-k keyfile] archive [outdir]
```

`-m` accepts `PCOMPRESS0..3` or `0..3`; `-a/-o/-v` accept algorithm names or
numeric codes. `-p` is required for password algorithms. `-k` is optional.
`-n niter` (0..7) is the cascade KDF multiplier (archive header byte 9).
