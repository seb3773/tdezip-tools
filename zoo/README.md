# ZOO Archiver 2.10 (AMD64 / Linux)

Rahul Dhesi's classic ZOO file archiver and recovery suite for Linux x86_64, patched for modern 64-bit systems and POSIX compliance.

## Features
- **Lempel-Ziv compression** with support for multiple file generations (`-add`, `-extract`, `-list`, `-test`).
- **Archive integrity** testing and repair utility (`fiz`).
- **Security & Portability**: includes all Debian security and 64-bit portability patches (preventing buffer overflows, directory traversal, AMD64 pointer mismatches, and format string vulnerabilities).

## Building from source

```bash
# Build binaries (zoo, fiz) and run self-tests:
./build.sh

# Build Debian package (.deb):
./build_deb.sh
```

## Binaries Provided
- `/usr/bin/zoo` : Main archiver
- `/usr/bin/fiz` : Damage analyzer and repair tool
