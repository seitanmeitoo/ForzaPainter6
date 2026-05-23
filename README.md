# Forza Painter 6

Outil qui convertit une image en formes géométriques (rectangles + ellipses tournées) et les amène dans l'éditeur de vinyles de **Forza Horizon 6** — soit en exportant un JSON, soit en les **injectant directement dans la mémoire du jeu**.

> Application **native Windows en C pur** : un seul `.exe` autonome, GUI Nuklear + GDI, génération CPU multi-thread, aucune dépendance runtime. L'implémentation Python d'origine est conservée dans [legacy/](legacy/) à titre de référence.

## État

| Étape | Statut |
|---|---|
| Génération image → formes + GUI (natif C) | ✅ fait |
| Export JSON FH6 | ✅ fait |
| **Injection mémoire FH6 (étape 2b)** | 🚧 en cours |
| Import via contrôle souris (étape 2a) | ⏸️ repoussé / probablement abandonné |
| Génération Python + GUI Tkinter (étape 1) | ✅ archivée dans [legacy/](legacy/) |

## Fonctionnalités

### Génération
- Charge n'importe quelle image (PNG / JPG / BMP)
- Détection automatique du canal alpha. Si l'image est transparente, choix entre :
  - **Garder la transparence** (mode sticker) : aucune forme posée sur les zones vides
  - **Remplacer par une couleur opaque** (color picker)
- **6 presets** de qualité ajustables par sliders ; nombre de formes plafonné à **3000** (limite FH6)
- **6 types de formes** : rectangles et ellipses tournées (natifs FH6, sans perte) ; cercles, ellipses, carrés tournés, triangles (convertis/approximés à l'export)
- Génération en arrière-plan avec preview live et progression

## Build & lancement

Pré-requis : **MSYS2 UCRT64** avec GCC. Depuis le shell MSYS2 UCRT64 :

```bash
cd native
make release      # produit ./vinyl-painter.exe (GUI, optimisé -O3 + LTO)
make run          # lance l'exe
make debug        # build console avec symboles (prints visibles)
```

Détails (windres, icône, manifest DPI) dans [native/README.md](native/README.md).

## Version Python (legacy)

L'ancienne application (GUI Tkinter + moteur de génération en Python, accélération GPU CuPy optionnelle) reste fonctionnelle dans [legacy/](legacy/) :

```
pip install -r legacy/requirements.txt
python legacy/main.py
```

## Crédits

Voir [NOTICES.md](NOTICES.md) pour la chaîne complète d'attribution.
