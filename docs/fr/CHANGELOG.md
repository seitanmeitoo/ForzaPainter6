[🇬🇧 English](../../CHANGELOG.md) | **🇫🇷 Français**

# Changelog

Tous les changements notables du projet **Forza Painter 6** depuis le début.

Format inspiré de [Keep a Changelog](https://keepachangelog.com/fr/1.0.0/).
Le projet n'utilise pas de versions sémantiques ; les entrées sont regroupées
par date et suivent le découpage en **étapes** et **phases** sont seulement indicatif.

---

## 2026-05-27 — Distribution, sticker strict, parallélisme et injection mémoire

### Ajouté
- **Étape 2 — Injection mémoire FH6** : module C natif
  `native/src/fh6_inject.{c,h}` intégré à la GUI. Lit un JSON FH6 exporté et
  écrit les formes directement dans la mémoire du processus du jeu via
  `WriteProcessMemory` (porté depuis ForzaDesigner6 « Multi-Support v3456 », MIT).
  - Localisation du `CLiveryGroup` : empreinte sphères (scan `u16 == count`) +
    fallback RTTI (`.?AVCLiveryGroup@@`), validation stricte avant toute écriture.
  - Offsets confirmés FH6 build **354.221**, surchargeables à chaud via
    `fh6_inject.cfg`.
  - Deux boutons GUI : « Localiser le groupe (test) » (lecture seule) et
    « Injecter (JSON) ». Admin requis seulement si `OpenProcess` échoue
    (MS Store/Xbox) ; Steam OK sans élévation.
- **Phase 15 — Parallel hill_climb multi-start** : le pool de workers
  gère un job `JOB_MUTATE` en plus de `JOB_RANDOM`. `hill_climb_mt` lance N chaînes
  de raffinement indépendantes depuis la même shape de base et garde la meilleure
  → qualité ≥ séquentiel (RMS inchangé) en exploitant les cœurs inactifs.
- **Phase 15 — Color picker fond opaque** : en mode opaque (image à
  alpha, sticker décoché), choix d'une couleur de fond personnalisée via
  `nk_color_picker` au lieu de la médiane par canal. L'alpha est composé sur cette
  couleur, le canvas s'initialise dessus et l'export FH6 l'utilise comme fond.
- **Phase 14 — Polish distribution** : build release en sous-système
  Windows GUI (`-mwindows`, plus de console noire), icône `.ico` multi-tailles et
  manifest DPI-aware PerMonitorV2 via `windres`. Ajout de `tools/make_icon.py`.

### Modifié / Corrigé
- **Filtre sticker strict** : `STICKER_OVERLAP_MIN` passe de `0.995` à
  `1.0` — une shape est rejetée dès qu'un seul pixel sort de la zone opaque,
  supprimant les débordements visibles (coins de rectangles) en mode sticker.

---

## 2026-05-22 — Refonte native C : phases 4 à 13

### Ajouté
- **Phase 13 — Mode sticker natif** : préservation de l'alpha en C.
  `image_extract_alpha_mask` produit un masque binaire ; `scoring` gagne un
  paramètre `alpha_mask` optionnel (RMS/baseline/couleur optimale restreints à la
  silhouette, filtre `body_inside/body_total`). L'`Engine` possède une copie owned
  du target et du masque ; canvas init gris uniforme. Checkbox GUI affichée
  seulement si alpha détecté ; export FH6 avec fond transparent `(0,0,0,0)`.
- **Phase 12 — Export JSON FH6** : `fh6_export.{c,h}`, sérialisation
  manuelle (sans cJSON). Conversion `ShapeType` → type FH6 (CIRCLE/ELLIPSE/
  ELLIPSE_ROT/RECT sans perte, RECT_ROT/TRIANGLE englobants = lossy). Bouton
  « Exporter JSON FH6… » (`GetSaveFileNameW`), `MessageBox` d'avertissement si
  formes lossy. Format identique au `fh6_export.py` legacy.
- **Phase 11 — Pool de workers Win32** : `best_of_random_mt` répartit
  les N candidats sur un pool de threads persistants (`EngineConfig.n_threads`,
  auto-détecté). Chaque worker a son `Rng` et son buffer scratch ; synchronisation
  par events Win32, sans lock sur le job partagé. Bench Équilibré 1500 formes :
  40 s mono-thread → 10 s sur 12 threads (~4×). Spinner « Threads » dans la GUI.
- **Phase 10 — Combo Preset → params engine** : table des 6 presets
  portée (`profile.h`), resync auto des spinners au changement de preset, nouveaux
  spinners « Candidats random » et « Mutations ». `FH6_MAX_SHAPES = 3000`.
- **Phase 9 — 5 nouveaux types de formes + checkboxes** : portage des
  ShapeOps Rectangle (axis & tourné), Ellipse (axis & tournée), Triangle.
  `rng_gauss` (Box-Muller) pour les mutations. `EngineConfig.types_mask`,
  `pick_type` uniforme parmi les bits actifs. 6 checkboxes GUI (défaut RECT +
  ELLIPSE_ROT), warning si aucun type coché.
- **Phase 8 — Engine asynchrone** : `threads.{c,h}`, `engine_run`
  hébergé dans un thread worker Win32. Communication via `CRITICAL_SECTION` +
  buffer preview single-slot, preview live (~20 updates/run), annulation réactive,
  quit propre en plein run. Boutons « Générer »/« Annuler » tous deux fonctionnels.
- **Phase 7 — Engine mono-thread** : `engine.{c,h}`, portage scalaire
  de `engine.py` (`best_of_random` → `hill_climb` → `apply_shape`). Heuristiques
  portées (alpha adaptatif, size_scale par progression, canvas init médian).
- **Phase 6 — Scoring delta bbox-local** : `scoring.{c,h}`, baseline en
  `double` (précision 4K), couleur optimale closed-form, formule delta
  `baseline - region_old + region_new` ne touchant que la bbox. Validé 5/5
  (prédit vs réel) via un bouton de test.
- **Phase 5 — Shape vtable + Circle** : `rng.{c,h}` (xorshift128+ seedé
  splitmix64, Lemire débiaisé), `util.h` (clamp/min/max inline), `shapes.{c,h}`
  (vtable `ShapeOps`, `SHAPE_REGISTRY`), Circle complet, bouton de test « 10 cercles ».
- **Phase 4 — Chargement image + preview Nuklear** : `image_io.{c,h}`
  (wrapper `stb_image`, `_wfopen` Unicode-safe, détection alpha fine), bouton
  « Choisir image… », layout 2 panneaux, `build_preview_image` corrigeant deux bugs
  GDI du backend (swap R/B et cisaillement diagonal), `StretchBlt` en `HALFTONE`.
  Vendoring de `stb_image.h`.

---

## 2026-05-22 — Passe B, bascule en legacy/ et amorce de la refonte native

### Ajouté
- **Amorce de la refonte native C** : nouveau dossier `native/` (C pur,
  CPU, GUI Nuklear+GDI, `.exe` Windows autonome). Makefile MinGW-w64/UCRT64
  (release `-O3 -ffast-math -march=native -flto`), `main.c` (fenêtre Win32 +
  Nuklear), vendoring de `nuklear.h`/`nuklear_gdi.h`. Build smoke OK (278 Ko,
  zéro DLL MSYS au runtime).
- **Passe B — batching natif des candidats (Python)** : 6 Shapes ×
  méthodes batch (`random_batch`, `rasterize_batch`, `mutate_batch`,
  `params_to_instance`, `to_params_row`), helpers multi-type dans `base.py`,
  `score_masks_batch` (1 passe vectorisée via `einsum`, aucune sync D2H interne),
  Engine GPU-first (`_best_of_random` batché, `_hill_climb` → parallel hill climb
  K=64), suppression du `ThreadPoolExecutor`.

### Modifié
- **Bascule du Python en `legacy/`** : tout le code Python de l'étape 1
  (`main.py`, `gui.py`, `image_utils.py`, `fh6_export.py`, `shapegen/`) déplacé dans
  `legacy/` à titre de référence. `.gitignore` mis à jour.
- **Fix add-ons GPU NVIDIA** (`70953c0`, PR #1 `fbe37f2`).
- Nettoyage des répertoires `__pycache__` suivis par erreur (`a406c0c`, `6234319`).

---

## 2026-05-20 — Étape 1 : image → JSON géométrique FH6 (Python)

### Ajouté
- **Commit initial** : moteur de génération de formes porté depuis
  ForzaDesigner6 (MIT), pur Python + NumPy, sans dépendance binaire. Restreint aux
  deux types FH6 (rectangle axis-aligned `type=1`, ellipse tournée `type=16`).
  UI Tkinter : chargement image + détection alpha, modes sticker / fond opaque,
  filtrage des types avant génération, preview live, progression, annulation propre,
  export vers chemin libre.
- **6 types de formes et 6 presets de qualité** : ajout de
  RotatedRectangle, Circle, Ellipse (axis), Triangle avec conversion intelligente à
  l'export (`to_fh6_payload` renvoie `(payload, lossy_count)`, confirmation UI si
  perte). Presets : Aperçu / Rapide / Équilibré / Détaillé / Qualité / Ultra qualité.
- **Accélération GPU optionnelle via CuPy (passe A)** : module
  `shapegen/xp.py` abstrayant le module array (numpy/cupy), `EngineConfig.use_gpu`,
  conversion CPU des canvas yieldés. Checkbox GUI grisée si CuPy/GPU absent.
- **Heuristiques de fidélité visuelle** : tailles initiales adaptatives
  (`size_scale` 3.0 → 1.0), couleur de fond médiane, opacité adaptative (alpha
  220 → 100). RMS sur logo 160×160 / 200 formes : 109 → 2.5 (−97,7 %).

### Performance
- **Cache du baseline de scoring (passe B initiale)** :
  `precompute_baseline()` calcule `diff_out_squared_sum` une seule fois par appel à
  `_best_of_random`/`_hill_climb`. Speedup CPU mesuré ~1,27× (200 formes / 128×128).

### Corrigé
- **Plafond FH6 de 3000 formes** : constante `FH6_MAX_SHAPES = 3000`,
  Spinbox UI borné, clamp défensif, presets Qualité/Ultra qualité réajustés
  (2800 / 3000).
