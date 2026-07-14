[🇬🇧 English](../../CONTRIBUTING.md) | **🇫🇷 Français**

# Contribuer

Forza Painter 6 est maintenu par une seule personne, sur son temps libre. Les issues et pull requests sont les bienvenues, mais la relecture peut prendre du temps.

## Signaler un bug

Ouvrez une issue GitHub avec :
- Le numéro de build de Forza Horizon 6 (pertinent pour les problèmes d'injection mémoire)
- Les étapes de reproduction
- Pour un crash : la sortie console d'un build `make debug` (voir [native/README.md](native/README.md)) si possible

## Builder depuis les sources

Voir [native/README.md](native/README.md) pour le build C natif (MSYS2 UCRT64) et [legacy/README.md](legacy/README.md) pour la version Python archivée.

## Conventions de code

- Application native (`native/`) : C11, aucune dépendance runtime hors ce que fournit MSYS2/UCRT64. Garder la contrainte d'un seul `.exe`, zéro DLL.
- Application legacy (`legacy/`) : conservée telle quelle à titre de référence, plus de développement actif dessus.
- Documenter les changements notables dans [CHANGELOG.md](CHANGELOG.md).

## Pull requests

- Des changements ciblés, un seul sujet par PR.
- Tester sur Windows avant de soumettre (le projet est Windows-only : GUI Win32 + `WriteProcessMemory`).
- Les petits fixes (typos, docs) peuvent partir directement en PR ; pour des changements plus gros, ouvrir une issue d'abord pour discuter de l'approche.
