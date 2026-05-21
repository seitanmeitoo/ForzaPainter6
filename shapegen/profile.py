from __future__ import annotations

from dataclasses import dataclass, field


# Types natifs FH6 (export sans perte) en premier ; les autres sont approximés
# par fh6_export à l'écriture du JSON.
SUPPORTED_SHAPE_TYPES = (
    "rectangle", "rotated_ellipse",
    "rotated_rectangle", "circle", "ellipse", "triangle",
)


# Types par défaut cochés dans l'UI (les natifs FH6 uniquement).
DEFAULT_SHAPE_TYPES = ("rectangle", "rotated_ellipse")


# Types convertis avec perte visuelle lors de l'export FH6.
LOSSY_SHAPE_TYPES = ("rotated_rectangle", "triangle")


@dataclass
class Profile:
    name: str = "default"
    description: str = "Default profile"
    max_threads: int = 0
    mutated_samples: int = 200
    preview_every: int = 25
    random_samples: int = 1000
    save_at: list[int] = field(default_factory=list)
    save_every: int = 0
    stop_at: int = 1500
    shape_types: list[str] = field(default_factory=lambda: list(DEFAULT_SHAPE_TYPES))


PRESETS: dict[str, Profile] = {
    "Aperçu": Profile(
        name="Aperçu", description="Test express, ≤ 10 s",
        random_samples=150, mutated_samples=40, stop_at=200, preview_every=10,
    ),
    "Rapide": Profile(
        name="Rapide", description="Brouillon rapide",
        random_samples=300, mutated_samples=80, stop_at=500, preview_every=15,
    ),
    "Équilibré": Profile(
        name="Équilibré", description="Bon compromis qualité / vitesse",
        random_samples=1000, mutated_samples=200, stop_at=1500, preview_every=25,
    ),
    "Détaillé": Profile(
        name="Détaillé", description="Bonne fidélité",
        random_samples=1500, mutated_samples=300, stop_at=2500, preview_every=40,
    ),
    "Qualité": Profile(
        name="Qualité", description="Rendu de production",
        random_samples=2500, mutated_samples=500, stop_at=2800, preview_every=50,
    ),
    "Ultra qualité": Profile(
        name="Ultra qualité", description="Limite FH6 (3000 formes), très lent",
        random_samples=4000, mutated_samples=800, stop_at=3000, preview_every=75,
    ),
}


# L'éditeur de vinyles FH6 n'accepte pas plus de 3000 formes par vinyle.
# Cette limite est imposée à la fois côté UI (Spinbox) et documentée dans les presets.
FH6_MAX_SHAPES = 3000


DEFAULT_PRESET = "Équilibré"
