# Freeze / Melt (version 2.5.0)

Historic Unix compression and decompression utility by Leonid Broukhis (1990-1993).

## Features
- **Algorithm**: Modified LZSS (8192-byte sliding buffer, maximum match length 256) combined with dynamic Huffman coding.
- **File extensions**: `.F`, `.tar.F`
- **Magic signature**: `0x1f 0x9f` (`frozen file 2.1`)
- **Characteristics**: Fast, lightweight, full streaming CLI support (`-c`, `-d`), companion tool for TdeZip.

## Modern POSIX Port
Updated for modern glibc and GCC:
- Replaced ancient internal `FILE->_cnt` direct buffer manipulation with standard POSIX buffered I/O.
- Modernized function prototypes and added POSIX system headers (`<stdlib.h>`, `<string.h>`, `<unistd.h>`, `<signal.h>`).

## Building & Packaging
To build the standalone `.deb` package:
```bash
./build_deb.sh
```
The resulting package will be stored in `../pool/main/f/freeze/freeze_2.5.0-1_amd64.deb`.
