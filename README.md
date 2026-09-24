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

Install an archive engine (e.g. LHA with full creation support):

```bash
sudo apt-get install lha
```

---

## Available and Upcoming Tools

| Tool | Version | Status | Description |
| :--- | :--- | :--- | :--- |
| **`lha`** | 1.14i-11 | Available | Pure native C LHA/LZH archiver (96 KB binary, zero dependencies, zero Java). Supports creation (`-a`), update (`-u`), extraction (`-x`), and testing (`-t`). |
| **`pea`** | 1.0 (C) | In Progress | Lightweight, pure C port of the PEA format engine, replacing the legacy FreePascal binary. |
| **`uharc`** | 0.6b (C) | In Progress | Pure native C UHARC archiver re-engineered under EU interoperability law (ALZ decoder & encoder). |

---

## Source Code & Building from Source

Each tool has its own dedicated directory containing the full C source code and an automated build script:

```bash
cd lha
./build_deb.sh
```

To refresh the APT repository index after adding or updating packages:

```bash
./update_repo.sh
```
