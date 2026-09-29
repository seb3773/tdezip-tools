# CMIX for TdeZip / Trinity Desktop Environment (TDE)

This directory provides the packaging and integration of Byron Knoll's **CMIX** (version 21) extreme lossless data compression utility for the TdeZip companion tools repository.

## Overview
- **Author**: Byron Knoll (http://www.byronknoll.com/cmix.html)
- **Upstream Repository**: https://github.com/byronknoll/cmix
- **Algorithm**: Advanced Ensemble Context Mixing + Recurrent LSTM Neural Network
- **Performance**: World-record holding compression ratio. Highly demanding on CPU and memory (at least 32 GB RAM recommended).
- **Features**:
  - Full CLI support (`-c`, `-d`, `-t`, `-n`, `-s`)
  - Integrated integrity testing (`-t`)
  - Stdin/stdout pipe support compatible with GNU Tar (`tar --use-compress-program=cmix`)
  - Zero external dependencies beyond libc and libstdc++

## Building
```bash
./build.sh
```

## Packaging Debian DEB
```bash
./build_deb.sh
```
