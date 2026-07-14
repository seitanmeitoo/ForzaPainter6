**🇬🇧 English** | [🇫🇷 Français](docs/fr/CHANGELOG.md)

# Changelog

All notable changes to the **Forza Painter 6** project since the beginning.

Format inspired by [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
The project does not use semantic versioning; entries are grouped by date and
follow the **step**/**phase** breakdown, which is indicative only.

---

## 2026-05-27 — Distribution, strict sticker mode, parallelism and memory injection

### Added
- **Step 2 — FH6 memory injection**: native C module
  `native/src/fh6_inject.{c,h}` integrated into the GUI. Reads an exported FH6
  JSON file and writes the shapes directly into the game process's memory via
  `WriteProcessMemory` (ported from ForzaDesigner6 "Multi-Support v3456", MIT).
  - `CLiveryGroup` location: sphere fingerprint (scan `u16 == count`) + RTTI
    fallback (`.?AVCLiveryGroup@@`), strict validation before any write.
  - Offsets confirmed on FH6 build **354.221**, hot-overridable via
    `fh6_inject.cfg`.
  - Two GUI buttons: "Locate group (test)" (read-only) and "Inject (JSON)".
    Admin rights only required if `OpenProcess` fails (MS Store/Xbox); Steam
    works without elevation.
- **Phase 15 — Parallel multi-start hill_climb**: the worker pool now
  handles a `JOB_MUTATE` job alongside `JOB_RANDOM`. `hill_climb_mt` launches N
  independent refinement chains from the same base shape and keeps the best one
  → quality ≥ sequential (RMS unchanged) while using otherwise idle cores.
- **Phase 15 — Opaque background color picker**: in opaque mode (image
  with alpha, sticker unchecked), choice of a custom background color via
  `nk_color_picker` instead of the per-channel median. Alpha is composited onto
  this color, the canvas is initialized on it, and the FH6 export uses it as
  the background.
- **Phase 14 — Distribution polish**: release build in the Windows GUI
  subsystem (`-mwindows`, no more black console window), multi-size `.ico` icon
  and DPI-aware PerMonitorV2 manifest via `windres`. Added `tools/make_icon.py`.

### Changed / Fixed
- **Strict sticker filter**: `STICKER_OVERLAP_MIN` raised from `0.995` to
  `1.0` — a shape is now rejected as soon as a single pixel falls outside the
  opaque area, removing visible overflow (rectangle corners) in sticker mode.

---

## 2026-05-22 — Native C rewrite: phases 4 to 13

### Added
- **Phase 13 — Native sticker mode**: alpha preservation in C.
  `image_extract_alpha_mask` produces a binary mask; `scoring` gains an optional
  `alpha_mask` parameter (RMS/baseline/optimal color restricted to the
  silhouette, `body_inside/body_total` filter). `Engine` owns a copy of the
  target and the mask; canvas is initialized to uniform gray. GUI checkbox
  shown only if alpha is detected; FH6 export with transparent background
  `(0,0,0,0)`.
- **Phase 12 — FH6 JSON export**: `fh6_export.{c,h}`, manual serialization
  (no cJSON). `ShapeType` → FH6 type conversion (CIRCLE/ELLIPSE/ELLIPSE_ROT/RECT
  lossless, RECT_ROT/TRIANGLE bounding shapes = lossy). "Export FH6 JSON…"
  button (`GetSaveFileNameW`), `MessageBox` warning if any shape is lossy.
  Format identical to the legacy `fh6_export.py`.
- **Phase 11 — Win32 worker pool**: `best_of_random_mt` distributes the N
  candidates across a pool of persistent threads (`EngineConfig.n_threads`,
  auto-detected). Each worker has its own `Rng` and scratch buffer;
  synchronization via Win32 events, no lock on the shared job. Balanced preset
  benchmark, 1500 shapes: 40 s single-threaded → 10 s on 12 threads (~4×).
  "Threads" spinner in the GUI.
- **Phase 10 — Preset combo → engine params**: the 6-preset table ported
  (`profile.h`), spinners auto-resync on preset change, new "Random candidates"
  and "Mutations" spinners. `FH6_MAX_SHAPES = 3000`.
- **Phase 9 — 5 new shape types + checkboxes**: ported ShapeOps for
  Rectangle (axis & rotated), Ellipse (axis & rotated), Triangle. `rng_gauss`
  (Box-Muller) for mutations. `EngineConfig.types_mask`, `pick_type` uniform
  among active bits. 6 GUI checkboxes (RECT + ELLIPSE_ROT checked by default),
  warning if no type is checked.
- **Phase 8 — Asynchronous engine**: `threads.{c,h}`, `engine_run` hosted
  in a Win32 worker thread. Communication via `CRITICAL_SECTION` + single-slot
  preview buffer, live preview (~20 updates/run), responsive cancellation,
  clean quit mid-run. "Generate"/"Cancel" buttons both functional.
- **Phase 7 — Single-threaded engine**: `engine.{c,h}`, scalar port of
  `engine.py` (`best_of_random` → `hill_climb` → `apply_shape`). Ported
  heuristics (adaptive alpha, progression-based size_scale, median canvas
  init).
- **Phase 6 — Local bbox delta scoring**: `scoring.{c,h}`, baseline in
  `double` (4K precision), closed-form optimal color, delta formula
  `baseline - region_old + region_new` touching only the bbox. Validated 5/5
  (predicted vs actual) via a test button.
- **Phase 5 — Shape vtable + Circle**: `rng.{c,h}` (xorshift128+ seeded by
  splitmix64, Lemire-debiased), `util.h` (inline clamp/min/max), `shapes.{c,h}`
  (`ShapeOps` vtable, `SHAPE_REGISTRY`), full Circle implementation, "10
  circles" test button.
- **Phase 4 — Image loading + Nuklear preview**: `image_io.{c,h}` (`stb_image`
  wrapper, Unicode-safe `_wfopen`, fine-grained alpha detection), "Choose
  image…" button, 2-panel layout, `build_preview_image` fixing two GDI backend
  bugs (R/B channel swap and diagonal shear), `StretchBlt` in `HALFTONE` mode.
  Vendored `stb_image.h`.

---

## 2026-05-22 — Pass B, switch to legacy/ and start of the native rewrite

### Added
- **Start of the native C rewrite**: new `native/` folder (pure C, CPU,
  Nuklear+GDI GUI, standalone Windows `.exe`). MinGW-w64/UCRT64 Makefile
  (release `-O3 -ffast-math -march=native -flto`), `main.c` (Win32 window +
  Nuklear), vendored `nuklear.h`/`nuklear_gdi.h`. Smoke build OK (278 KB, zero
  MSYS DLLs at runtime).
- **Pass B — native candidate batching (Python)**: 6 Shapes × batch
  methods (`random_batch`, `rasterize_batch`, `mutate_batch`,
  `params_to_instance`, `to_params_row`), multi-type helpers in `base.py`,
  `score_masks_batch` (1 vectorized pass via `einsum`, no internal D2H sync),
  GPU-first Engine (`_best_of_random` batched, `_hill_climb` → parallel hill
  climb K=64), removed `ThreadPoolExecutor`.

### Changed
- **Switched Python code to `legacy/`**: all step-1 Python code (`main.py`,
  `gui.py`, `image_utils.py`, `fh6_export.py`, `shapegen/`) moved to `legacy/`
  for reference. `.gitignore` updated.
- **Fixed NVIDIA GPU add-ons** (`70953c0`, PR #1 `fbe37f2`).
- Cleaned up `__pycache__` directories accidentally tracked (`a406c0c`,
  `6234319`).

---

## 2026-05-20 — Step 1: image → FH6 geometric JSON (Python)

### Added
- **Initial commit**: shape generation engine ported from ForzaDesigner6
  (MIT), pure Python + NumPy, no binary dependency. Restricted to the two FH6
  types (axis-aligned rectangle `type=1`, rotated ellipse `type=16`). Tkinter
  UI: image loading + alpha detection, sticker / opaque background modes, type
  filtering before generation, live preview, progress, clean cancellation,
  export to a chosen path.
- **6 shape types and 6 quality presets**: added RotatedRectangle, Circle,
  Ellipse (axis), Triangle with smart export conversion (`to_fh6_payload`
  returns `(payload, lossy_count)`, UI confirmation if lossy). Presets:
  Preview / Fast / Balanced / Detailed / Quality / Ultra quality.
- **Optional GPU acceleration via CuPy (pass A)**: `shapegen/xp.py` module
  abstracting the array module (numpy/cupy), `EngineConfig.use_gpu`, CPU
  conversion of yielded canvases. GUI checkbox grayed out if CuPy/GPU is
  absent.
- **Visual fidelity heuristics**: adaptive initial sizes (`size_scale` 3.0
  → 1.0), median background color, adaptive opacity (alpha 220 → 100). RMS on
  a 160×160 logo / 200 shapes: 109 → 2.5 (−97.7%).

### Performance
- **Scoring baseline cache (initial pass B)**: `precompute_baseline()`
  computes `diff_out_squared_sum` once per call to
  `_best_of_random`/`_hill_climb`. Measured CPU speedup ~1.27× (200 shapes /
  128×128).

### Fixed
- **FH6 3000-shape cap**: `FH6_MAX_SHAPES = 3000` constant, bounded UI
  Spinbox, defensive clamp, Quality/Ultra quality presets readjusted (2800 /
  3000).
