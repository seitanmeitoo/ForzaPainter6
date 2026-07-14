**🇬🇧 English** | [🇫🇷 Français](docs/fr/README.md)

# Forza Painter 6

Tool that converts an image into geometric shapes (rectangles + rotated ellipses) and brings them into the **Forza Horizon 6** livery editor — either by exporting a JSON file, or by **injecting them directly into the game's memory**.

> **Native Windows app written in pure C**: a single standalone `.exe`, Nuklear + GDI GUI, multi-threaded CPU generation, no runtime dependency. The original Python implementation is kept in [legacy/](legacy/) for reference.

## Status

| Step | Status |
|---|---|
| Image → shapes generation + GUI (native C) | ✅ done |
| FH6 JSON export | ✅ done |
| **FH6 memory injection (step 2)** | ✅ done |
| Python generation + Tkinter GUI (step 1) | ✅ archived in [legacy/](legacy/) |

## Features

### Generation
- Loads any image (PNG / JPG / BMP)
- Automatic alpha channel detection. If the image is transparent, choose between:
  - **Keep transparency** (sticker mode): no shape placed on empty areas
  - **Replace with an opaque color** (color picker)
- **6 quality presets** adjustable via sliders; shape count capped at **3000** (FH6 limit)
- **6 shape types**: rectangles and rotated ellipses (native to FH6, lossless); circles, ellipses, rotated squares, triangles (converted/approximated on export)
- Background generation with live preview and progress

### Getting shapes into FH6
- **JSON export** compatible with FH6 / forza-painter (`Export FH6 JSON...`)
- **Direct memory injection (step 2)**: writes the shapes into the game process via `WriteProcessMemory`, no manual input needed. Locates the livery group dynamically (sphere fingerprint + RTTI fallback) and **validates before writing** (refuses if no reliable candidate is found, to avoid corrupting game state). Offsets confirmed on FH6 build 354.221, overridable without recompiling.

## Build & run

Requirements: **MSYS2 UCRT64** with GCC. From the MSYS2 UCRT64 shell:

```bash
cd native
make release      # produces ./vinyl-painter.exe (GUI, optimized -O3 + LTO)
make run          # launches the exe
make debug        # console build with symbols (prints visible)
```

Details (windres, icon, DPI manifest) in [native/README.md](native/README.md).

## Python version (legacy)

The old application (Tkinter GUI + Python generation engine, optional CuPy GPU acceleration) remains functional in [legacy/](legacy/):

```
pip install -r legacy/requirements.txt
python legacy/main.py
```

## Credits

See [NOTICES.md](NOTICES.md) for the full attribution chain.
