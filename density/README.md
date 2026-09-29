# Density Compression Utility

`density` is a standalone CLI compression utility based on Guillaume Vaudaux's
[Density](https://github.com/g1mv/density) library.

Packaged as a companion tool for **TdeZip** / Trinity Desktop Environment (TDE).

## Features
- Ultra-fast compression and decompression using 4-byte work units.
- 3 selectable algorithms:
  - `-1`, `--chameleon`: Chameleon (fastest, dictionary lookup, GB/s order).
  - `-2`, `--cheetah`: Cheetah (balanced speed/ratio, dual swapped dictionary + prediction, default).
  - `-3`, `-9`, `--lion`, `--best`: Lion (best compression ratio, rank entropy).
- Chunked 2 MB streaming format with IEEE 802.3 CRC32 block checksums.
- Automatic decompression without needing algorithm specification.
- Full GNU tar compatibility (`tar --use-compress-program=density`).
