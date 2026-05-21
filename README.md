# vinyl-painter

Outil Python qui convertit une image en formes géométriques (rectangles + ellipses tournées) et exporte un JSON compatible avec l'éditeur de vinyles de **Forza Horizon 6**.

## Fonctionnalités

- Charge n'importe quelle image (PNG / JPG / BMP)
- Détecte automatiquement le canal alpha. Si l'image est transparente, choix entre :
  - **Garder la transparence** (mode sticker) : aucune forme n'est posée sur les zones vides
  - **Remplacer par une couleur opaque** : color picker
- **6 presets** de qualité (Aperçu, Rapide, Équilibré, Détaillé, Qualité, Ultra qualité), ajustables finement via les sliders ; nombre de formes plafonné à **3000** (limite de l'éditeur FH6)
- **6 types de formes** sélectionnables :
  - Natifs FH6 (sans perte à l'export) : rectangles, ellipses tournées — cochés par défaut
  - Convertis à l'export : cercles, ellipses non tournées (conversion exacte) ; carrés tournés, triangles (approximés par ellipses tournées, perte visuelle signalée par un dialog au save)
- Génération en arrière-plan avec preview live et barre de progression
- Sauvegarde du JSON où on veut via `Enregistrer sous…`

## Installation

```
pip install -r requirements.txt
```

Python 3.10+ requis (Tkinter inclus dans la stdlib).

### Accélération GPU (optionnelle, NVIDIA)

Le moteur peut tourner sur GPU CUDA via **CuPy**. CuPy est une dépendance
optionnelle — l'app fonctionne parfaitement sans, mais la génération est
~2× plus rapide sur un GPU NVIDIA récent.

Installer le wheel CuPy correspondant à votre version de CUDA Toolkit :

```
pip install cupy-cuda12x      # CUDA 12.x
# ou
pip install cupy-cuda11x      # CUDA 11.x
```

Au lancement de `python main.py`, la checkbox **« Accélération GPU »** dans
la section Génération est :

- **Activable** si CuPy est installé et qu'un GPU NVIDIA est détecté (le nom
  du GPU est affiché en vert à côté)
- **Grisée** sinon, avec un message explicite

## Lancement

```
python main.py
```

## Crédits

Le moteur de génération est porté depuis [Forza Painter](https://github.com/forza-painter/forza-painter) (MIT). Voir [NOTICES.md](NOTICES.md) pour la chaîne complète d'attribution (forza-painter → geometrize-lib → Primitive).
