# LZAV - Fast Data Compression CLI Utility

This package provides a standalone, freestanding C command-line tool `lzav` for compressing, decompressing, and testing files using Aleksey Vaneev's LZAV (Lossless Audio-Visual) compression algorithm.

## Features
- Ultra-fast decompression throughput (multi-GB/s).
- Higher compression ratios than LZ4 and Snappy on many datasets.
- Fast default compression mode and high-ratio HI mode (`-2` / `--best`).
- Complete stream framing with CRC32 integrity verification.
- Full GNU Tar compatibility via `tar --use-compress-program=lzav`.
- Freestanding C99 implementation without external dependencies.

## Usage
```bash
# Compress file
lzav input.txt -o input.txt.lzav

# Decompress file
lzav -d input.txt.lzav -o input.txt

# Test integrity
lzav -t input.txt.lzav

# Tar archive creation
tar --use-compress-program=lzav -cf archive.tar.lzav files...
```
