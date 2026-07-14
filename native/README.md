**🇬🇧 English** | [🇫🇷 Français](../docs/fr/native/README.md)

# Forza Painter 6 (native C rewrite)

Native Windows implementation in pure C. No external runtime dependency — a single standalone `.exe`.

## Build

Requirements: **MSYS2 UCRT64** with GCC ≥ 13 (`pacman -S --needed base-devel mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-gcc`).

From the **MSYS2 UCRT64** shell:

```bash
cd native
make release      # produces ./forzapainter6.exe (optimized -O3 + LTO + native AVX, GUI subsystem)
make debug        # build with symbols -g -O0 (console subsystem, prints visible)
make run          # launches the exe
make clean        # removes build/ and the exe
```

`make release` also compiles `app.rc` via **windres** (bundled with the UCRT64 toolchain): it
embeds the icon (`icon.ico`) and the DPI-aware manifest (`app.manifest`, PerMonitorV2). The
release build switches to the Windows GUI subsystem (`-mwindows`) → no console window behind the
app. The debug build keeps the console to see `printf` output.

To regenerate the icon (translucent geometric shapes): `py tools/make_icon.py` (requires Pillow).

## Status

Rewrite nearly complete.
Completed features: Win32 + Nuklear/GDI window, image loading, 6 shape types, scoring,
multi-threaded engine, presets, sticker mode, FH6 JSON export, distribution polish (icon + DPI manifest + GUI), opaque background color picker, parallel hill_climb.
