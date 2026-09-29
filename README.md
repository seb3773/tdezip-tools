# TdeZip-Tools

Official APT repository for TdeZip archivers utilities.

**Repository Portal / GitHub Pages**: [https://seb3773.github.io/tdezip-tools/](https://seb3773.github.io/tdezip-tools/)

---

## APT Configuration

Add the repository to your system sources:

```bash
echo "deb [trusted=yes] https://seb3773.github.io/tdezip-tools/ stable main" | sudo tee /etc/apt/sources.list.d/tdezip-tools.list
sudo apt-get update
```

Install archive companion engines:

```bash
# Install native C LHA archiver (LZH/LHA):
sudo apt-get install lha

# Install native C PEA archiver:
sudo apt-get install pea-c

# Install Freeze / Melt archiver:
sudo apt-get install freeze

# Install ZOO archiver & recovery tool:
sudo apt-get install zoo

# Install LZHAM high-ratio compressor:
sudo apt-get install lzham

# Install Historic Unix Pack / Unpack / Pcat (.z):
sudo apt-get install pack
```

---

## Available and Upcoming Tools

| Tool | Version | Status | Description |
| :--- | :--- | :--- | :--- |
| **`lha`** | 1.14i-11 | Available | Pure native C LHA/LZH archiver (96 KB binary, zero dependencies, zero Java). Supports creation (`-a`), update (`-u`), extraction (`-x`), and testing (`-t`). |
| **`pea-c`** | 1.0-1 | Available | Dedicated pure native C PEA archiver (52 KB binary, replaces 7 MB FreePascal binary). Supports creation, extraction, testing, listing, AE-EAX encryption, cascades, and multi-volume archives. |
| **`freeze`** | 2.5.0-1 | Available | Historic Unix Freeze / Melt compressor (23 KB binary). Supports creation, extraction, and testing for `.F` and `.tar.F` (LZSS + Dynamic Huffman). |
| **`zoo`** | 2.10-28 | Available | Rahul Dhesi's classic ZOO archiver and repair suite (64 KB deb, `zoo` + `fiz`). Supports creation, extraction, testing, comments, multi-generations, and damaged archive recovery. |
| **`lzham`** | 1.0-1 | Available | Richard Geldreich's high-ratio LZ compressor (82 KB deb, `/usr/bin/lzham`). LZMA-class ratios with faster decompression, dictionaries up to 512 MB, multi-threading, Tar pipelines (`.tar.lzham`) and integrity verification. |
| **`pack`** | 1.0-1 | Available | Historical Unix Huffman compressor and decompressor suite (32 KB deb, `pack`, `unpack`, `pcat`). Authentic Research Unix V8 / System V algorithm modernized for 64-bit POSIX, full `.z` creation, extraction and test support. |
| **`uharc`** | 0.6b (C) | In Progress | Pure native C UHARC archiver re-engineered under EU interoperability law (ALZ decoder & encoder). |

---

## Source Code & Building from Source

Each tool has its own dedicated directory containing the full C/C++ source code and an automated build script:

```bash
# Build LHA:
cd lha && ./build_deb.sh && cd ..

# Build PEA-C:
cd pea-c && ./build_deb.sh && cd ..

# Build Freeze:
cd freeze && ./build_deb.sh && cd ..

# Build ZOO:
cd zoo && ./build_deb.sh && cd ..

# Build LZHAM:
cd lzham && ./build_deb.sh && cd ..

# Build Pack:
cd pack && ./build_deb.sh && cd ..
```

To refresh the APT repository index after adding or updating packages:

```bash
./update_repo.sh
```
