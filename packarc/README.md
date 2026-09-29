# packARC — Linux build notes

> Original source: packARC v0.7beta18 (12/17/2014) by Matthias Stirner / Se  
> License: LGPL v3

---

## Build requirements

| Tool | Minimum version tested |
|------|------------------------|
| g++ | 4.6 (tested with 12.2) |
| gcc | 4.6 (tested with 12.2) |
| make | any GNU make |
| ar / ranlib | binutils |

No external libraries required beyond the system libc/libstdc++.

---

## Build steps

The build is a three-stage process: static libraries → SFX stub → final binary.

```sh
# 1. Build the optimised packANY static library (used by the main binary)
cd source/packANY/packANYlib
make -f Makefile_lib_Linux
make -f Makefile_lib_Linux clean

# 2. Build the size-optimised packANY static library (used by the SFX stub)
make -f Makefile_lib_Os_Linux
make -f Makefile_lib_Os_Linux clean

# 3. Copy both .a files to the packARC source directory
cp packANYlib.a packANYlib_small.a ../../packARC/

# 4. Build the SFX stub (the self-extracting skeleton embedded in the binary)
cd ../../packARC
make -f Makefile_sfx_stub.linux
make -f Makefile_sfx_stub.linux clean

# 5. Convert the SFX stub binary into a C header
gcc -o sfxstub2h sfxstub2h.c
./sfxstub2h          # produces sfxstub.h

# 6. Build the final packARC binary
make -f Makefile.linux
make -f Makefile.linux clean
```

The resulting binary is `source/packARC/packARC` (~694 KB, ELF 64-bit, stripped).

### Compiler flags summary

| Flag | Purpose |
|------|---------|
| `-O3` | Full optimisation (main binary) |
| `-Os` | Size optimisation (SFX stub / small lib) |
| `-DBUILD_LIB` | Switches packANY sources to library mode |
| `-DSFX_STUB` | Switches frontend to SFX-extractor mode |
| `-funroll-loops -ffast-math -fsched-spec-load -fomit-frame-pointer` | Performance tuning |
| `-fvisibility=hidden` | Hides internal symbols from the static lib |
| `-s -static-libgcc` | Strip symbols, statically link libgcc |

---

## CLI reference

```
packARC [command] [switches] [archive] [files...]
```

### Commands

| Command | Description |
|---------|-------------|
| `a` | Add / replace files in archive |
| `d` | Delete files from archive |
| `c` | Convert archive ↔ SFX |
| `x` | Extract files from archive |
| `t` | Test archive integrity (CRC32) |
| `l` | List archive contents |

### Switches

| Switch | Description |
|--------|-------------|
| `--` | Stop processing switches |
| `-o` | Overwrite existing files on extraction |
| `-s` | Skip existing files (default) |
| `-r` | Rename on conflict (appends `_` to filename) |
| `-i` | (with `x`) Ignore CRC32 errors |
| `-sfx` | (with `a`) Create a self-extracting archive |
| `-sl` | (with `l`) Simple list format |
| `-sm` | (with `l`) MultiArc-compatible list format |
| `-csv` | (with `l`) CSV list format |
| `-C <dir>` | (with `x`) Extract to specified directory |
| `-np` | No pause after processing (now default) |

> **Note:** `-np` is the default in this build (`pause_f = false`). The interactive
> `< press ENTER >` prompt has been disabled so the tool is directly scriptable from
> a GUI or shell pipeline.

### Examples

```sh
# Add all JPEG files to a new archive
packARC a images.pja *.jpg

# Extract all files, overwriting existing ones
packARC x -o media.pja

# Extract to a specific directory
packARC x -o -C /tmp/output media.pja

# Extract specific files only
packARC x -o archive.pja photo1.jpg photo2.jpg

# Test archive integrity
packARC t archive.pja

# List contents (verbose)
packARC l archive.pja

# List contents (CSV)
packARC l -csv archive.pja

# Delete a file from archive
packARC d archive.pja unwanted.bin

# Create a self-extracting archive
packARC a -sfx music.exe *.mp3

# Convert existing archive to SFX (or back)
packARC c archive.pja
```

---

## Archive format

Archives use the `.pja` (packJPG Archive) format. The compression method
is chosen automatically per file:

| Input type | Algorithm | Typical gain |
|------------|-----------|-------------|
| JPEG (baseline, progressive, CMYK, YCbCr) | packJPG | ~20% |
| MPEG-1 Audio Layer III MP3 (≥32 kHz) | packMP3 | ~16% |
| BMP / PPM / PGM / PBM (binary, 8 or 16 bit) | packPNM | variable |
| All other / unrecognised types | packARI (arithmetic coding) | variable |
| Already-compressed files (PNG, etc.) | STO (stored as-is) | 0% |

---

## Modifications vs. upstream source

| File | Change |
|------|--------|
| [`source/packARC/frontend.cpp`](source/packARC/frontend.cpp) | `pause_f` default changed from `true` to `false` — removes interactive `< press ENTER >` prompt |
| [`source/packARC/frontend.cpp`](source/packARC/frontend.cpp) | Added `-C <dir>` switch: calls `chdir()` before extraction so output lands in the requested directory |
