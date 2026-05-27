# Forza Painter 6 (refonte native C)

Implémentation native Windows en C pur. Aucune dépendance externe runtime — un seul `.exe` autonome.

## Build

Pré-requis : **MSYS2 UCRT64** avec GCC ≥ 13 (`pacman -S --needed base-devel mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-gcc`).

Depuis le shell **MSYS2 UCRT64** :

```bash
cd native
make release      # produit ./forzapainter6.exe (optimisé -O3 + LTO + AVX natif, sous-système GUI)
make debug        # build avec symboles -g -O0 (sous-système console, prints visibles)
make run          # lance l'exe
make clean        # supprime build/ et l'exe
```

`make release` compile aussi `app.rc` via **windres** (fourni avec la toolchain UCRT64) : il
embarque l'icône (`icon.ico`) et le manifest DPI-aware (`app.manifest`, PerMonitorV2). Le build
release passe en sous-système Windows GUI (`-mwindows`) → pas de fenêtre console derrière l'app.
Le build debug garde la console pour voir les `printf`.

Pour régénérer l'icône (formes géométriques translucides) : `py tools/make_icon.py` (nécessite Pillow).

## État

Refonte quasi terminée.
Fonctionnalitées faites : fenêtre Win32 + Nuklear/GDI, chargement image, 6 types de formes, scoring,
engine multi-thread, presets, mode sticker, export JSON FH6, polish distribution (icône + manifest DPI + GUI), color picker fond opaque, parallel hill_climb.
