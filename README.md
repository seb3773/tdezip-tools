# TdeZip-Tools

Official APT repository and source tree for custom companion compression and archiving engines for **TdeZip** (TDE Archive Manager).

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
```

---

## Available and Upcoming Tools

| Tool | Version | Status | Description |
| :--- | :--- | :--- | :--- |
| **`lha`** | 1.14i-11 | Available | Pure native C LHA/LZH archiver (96 KB binary, zero dependencies, zero Java). Supports creation (`-a`), update (`-u`), extraction (`-x`), and testing (`-t`). |
| **`pea-c`** | 1.0-1 | Available | Dedicated pure native C PEA archiver (52 KB binary, replaces 7 MB FreePascal binary). Supports creation, extraction, testing, listing, AE-EAX encryption, cascades, and multi-volume archives. |
| **`freeze`** | 2.5.0-1 | Available | Historic Unix Freeze / Melt compressor (23 KB binary). Supports creation, extraction, and testing for `.F` and `.tar.F` (LZSS + Dynamic Huffman). |
| **`uharc`** | 0.6b (C) | In Progress | Pure native C UHARC archiver re-engineered under EU interoperability law (ALZ decoder & encoder). |

---

## Source Code & Building from Source

Each tool has its own dedicated directory containing the full C source code and an automated build script:

```bash
# Build LHA:
cd lha && ./build_deb.sh && cd ..

# Build PEA-C:
cd pea-c && ./build_deb.sh && cd ..

# Build Freeze:
cd freeze && ./build_deb.sh && cd ..
```

To refresh the APT repository index after adding or updating packages:

```bash
./update_repo.sh
```
