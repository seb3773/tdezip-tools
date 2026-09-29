# LZFSE (Apple Lempel-Ziv + Finite State Entropy)

Reference C implementation of the LZFSE data compression algorithm developed by Apple Inc.
LZFSE targets compression ratios comparable to Deflate (zlib level 5) with 2x to 3x higher speed and superior energy efficiency.

Packaging for Trinity Desktop Environment (TDE) / TdeZip companion tools:
- Pure native C (zero external library dependencies, libc only)
- Standalone CLI executable `lzfse` (~43 KB stripped)
- Supports both Apple native CLI syntax (`-encode`, `-decode`, `-i`, `-o`) and standard Unix streaming conventions (`-d`, `-t`, `-f`, `-c`)
- Full support for individual files (`.lzfse`), Unix pipelines, and Tar archives (`.tar.lzfse`, `.tlzfse`)
