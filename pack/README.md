# Unix Pack (`pack`, `unpack`, `pcat`)

Historical Unix Huffman encoding compressor and decompressor, adapted from Research Unix V8 / System V (T.G. Szymanski, Bell Labs 1978-1979).

Modernized for Trinity Desktop Environment (TDE) / TdeZip companion tools:
- Standard POSIX C, 64-bit architecture clean (x86_64, aarch64, etc.)
- Elimination of historical 14-character filename limit (`PATH_MAX` support)
- Addition of `-f` (`--force`) and `-c` (`--stdout`) flags
- Bidirectional compatibility with GNU `gzip` (`gzip -dc`, `gzip -t`)
- Includes `pack`, `unpack`, and `pcat` in a lightweight native package (~30 KB)
