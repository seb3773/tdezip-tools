# LZHAM Archiver & Compressor 1.0 (AMD64 / Linux)

Richard Geldreich's high-ratio lossless compression utility for Linux x86_64, built as a standalone native CLI tool for **TdeZip**.

## Features
- **High-ratio compression**: LZMA-class ratios with significantly faster decompression.
- **Large dictionaries**: Configurable window size from 32 KB up to 512 MB (`-d15` to `-d29`).
- **Multi-threaded**: Automatic multi-core acceleration (`-t`).
- **Full Unix pipeline & Tar support**: Works with `tar --use-compress-program=lzham` for `.tar.lzham` archives.
- **Integrity verification**: Integrated Adler-32 checksums (`-t`).

## Building from source

```bash
# Build binary (lzham) and run test suite:
./build.sh

# Build Debian package (.deb):
./build_deb.sh
```

## Binary Provided
- `/usr/bin/lzham` : Native compressor and decompressor CLI
