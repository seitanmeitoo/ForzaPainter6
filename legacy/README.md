**🇬🇧 English** | [🇫🇷 Français](../docs/fr/legacy/README.md)

# Forza Painter 6 (legacy Python), formerly vinyl-painter

Python tool that converts an image into geometric shapes (rectangles + rotated ellipses) and exports a JSON file compatible with the **Forza Horizon 6** livery editor.
The old application (Tkinter GUI + Python generation engine, optional CuPy GPU acceleration) remains functional.

## Features

- Loads any image (PNG / JPG / BMP)
- Automatically detects the alpha channel. If the image is transparent, choose between:
  - **Keep transparency** (sticker mode): no shape is placed on empty areas
  - **Replace with an opaque color**: color picker
- **6 quality presets** (Preview, Fast, Balanced, Detailed, Quality, Ultra quality), finely adjustable via sliders; shape count capped at **3000** (FH6 editor limit)
- **6 selectable shape types**:
  - Native to FH6 (lossless on export): rectangles, rotated ellipses — checked by default
  - Converted on export: circles, non-rotated ellipses (exact conversion); rotated squares, triangles (approximated by rotated ellipses, visual loss flagged by a dialog on save)
- Background generation with live preview and progress bar
- Save the JSON wherever you want via `Save as…`

## Installation

```
pip install -r requirements.txt
```

Python 3.10+ required (Tkinter included in the stdlib).

### GPU acceleration (optional, NVIDIA)

The engine can run on a CUDA GPU via **CuPy**. CuPy is an optional
dependency — the app works fine without it, but generation is ~2× faster on
a recent NVIDIA GPU.

Install the CuPy wheel matching your CUDA Toolkit version:

```
pip install cupy-cuda12x      # CUDA 12.x
# or
pip install cupy-cuda11x      # CUDA 11.x
```

When launching `python main.py`, the **"GPU acceleration"** checkbox in the
Generation section is:

- **Enabled** if CuPy is installed and an NVIDIA GPU is detected (the GPU
  name is shown in green next to it)
- **Grayed out** otherwise, with an explicit message

> In practice, GPU acceleration is slower than CPU computation because the GPU work is managed by the CPU, which sends its instructions with much more latency than the GPU takes to compute, then waits a long time for its next computation.

## Running

```
python main.py
```

## Status

Abandoned version.
