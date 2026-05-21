from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable
import random
import time

import numpy as np

from shapegen.profile import Profile
from shapegen.scoring import (
    composite, rms_error, score_shape, score_masks_batch, precompute_baseline,
)
from shapegen.shapes import (
    Shape, random_shape_batch, rasterize_batch_multi, locate_batch_index,
)
from shapegen.xp import get_xp, to_cpu


# Heuristiques pour améliorer la fidélité visuelle des petites générations.
# Alpha plus opaque quand peu de formes (rendu contrasté), plus translucide quand
# beaucoup (accumulation subtile). Interpolation linéaire entre les deux bornes.
_ALPHA_HIGH = 220     # à stop_at ≤ 200
_ALPHA_LOW = 100      # à stop_at ≥ 3000

# Tailles initiales adaptatives : les premières formes peuvent être ~3× plus
# grandes que la borne de fin, pour poser les grandes masses avant les détails.
_SIZE_SCALE_START = 3.0
_SIZE_SCALE_END = 1.0
# Fraction de la génération sur laquelle la rampe descend (0.5 = première moitié)
_SIZE_SCALE_RAMP = 0.5


def _alpha_for_stop_at(stop_at: int) -> int:
    if stop_at <= 200:
        return _ALPHA_HIGH
    if stop_at >= 3000:
        return _ALPHA_LOW
    t = (stop_at - 200) / (3000 - 200)
    return int(round(_ALPHA_HIGH + (_ALPHA_LOW - _ALPHA_HIGH) * t))


def _size_scale_for_progress(progress: float) -> float:
    """progress ∈ [0, 1]. Rampe linéaire de _SIZE_SCALE_START à _SIZE_SCALE_END
    sur la fraction _SIZE_SCALE_RAMP de la génération, plateau ensuite."""
    if progress >= _SIZE_SCALE_RAMP:
        return _SIZE_SCALE_END
    t = progress / _SIZE_SCALE_RAMP
    return _SIZE_SCALE_START + (_SIZE_SCALE_END - _SIZE_SCALE_START) * t


@dataclass
class EngineConfig:
    profile: Profile
    seed: int = 0
    use_gpu: bool = False


@dataclass
class EngineEvent:
    kind: str
    shape_count: int = 0
    rms: float = 0.0
    canvas: np.ndarray | None = None  # toujours en numpy côté événements (D2H si GPU)
    message: str = ""


class Engine:
    """Image → shapes generator. Iterable .run() yields EngineEvent objects.

    Set EngineConfig.use_gpu=True to allocate arrays on a CUDA GPU via CuPy
    (drop-in NumPy replacement). All math runs on the GPU until canvases are
    handed to callers via events — they're transferred back to CPU first.
    """

    def __init__(self, target_rgb: np.ndarray, config: EngineConfig, alpha_mask: np.ndarray | None = None) -> None:
        if target_rgb.ndim != 3 or target_rgb.shape[2] != 3:
            raise ValueError("target_rgb must be HxWx3 RGB uint8")
        self.config = config
        self.profile = config.profile
        self.xp = get_xp(config.use_gpu)
        self.h, self.w = target_rgb.shape[:2]
        # Move target / alpha into the active array module.
        self.target = self.xp.asarray(target_rgb, dtype=self.xp.uint8)
        self.alpha_mask = self.xp.asarray(alpha_mask) if alpha_mask is not None else None
        if self.alpha_mask is not None:
            mask3 = (self.alpha_mask > 0)[:, :, None]
            self.target = self.target * mask3.astype(self.xp.uint8)
            self.canvas = self.xp.full((self.h, self.w, 3), 40, dtype=self.xp.uint8)
        else:
            # Couleur de fond = médiane par canal. Plus robuste que la moyenne
            # arithmétique sur les images contrastées (ex. logo sur fond uni),
            # où la moyenne donne un gris boueux.
            flat = self.target.reshape(-1, 3).astype(self.xp.float32)
            median = self.xp.median(flat, axis=0).astype(self.xp.uint8)
            self.canvas = self.xp.tile(median, (self.h, self.w, 1)).astype(self.xp.uint8)
        self._alpha_init = _alpha_for_stop_at(self.profile.stop_at)
        self.shapes: list[Shape] = []
        self.rms = rms_error(self.canvas, self.target, self.alpha_mask, xp=self.xp)
        self.start_rms = self.rms
        self._stop = False
        seed = config.seed or int(time.time() * 1000) & 0xFFFFFFFF
        self.rng = random.Random(seed)

    def request_stop(self) -> None:
        self._stop = True

    def _current_size_scale(self) -> float:
        progress = len(self.shapes) / max(1, self.profile.stop_at)
        return _size_scale_for_progress(progress)

    def _best_of_random(self, types: list[str], n: int) -> Shape:
        """Batched : rasterise N candidats full-canvas et les score en une passe
        GPU. 1 seul argmin().get() à la fin → ~N× moins de syncs D2H."""
        size_scale = self._current_size_scale()
        batches, offsets = random_shape_batch(
            self.rng, self.w, self.h, types, n,
            size_scale=size_scale, alpha=self._alpha_init, xp=self.xp,
        )
        masks = rasterize_batch_multi(batches, self.w, self.h, xp=self.xp)
        baseline = precompute_baseline(self.canvas, self.target, self.alpha_mask, xp=self.xp)
        scores, colors = score_masks_batch(
            masks, self.canvas, self.target, self.alpha_mask,
            self._alpha_init, xp=self.xp, baseline=baseline,
        )
        best_idx = int(scores.argmin().get()) if hasattr(scores, 'get') else int(scores.argmin())
        cls, params_row_xp, color_row_xp = locate_batch_index(batches, offsets, best_idx)
        params_row = to_cpu(params_row_xp)
        color_row = to_cpu(color_row_xp)
        # On utilise la couleur OPTIMALE recalculée par score_masks_batch (pas
        # la couleur random initiale), comme le faisait l'ancien score_shape.
        color = tuple(int(c) for c in to_cpu(colors[best_idx]))
        return cls.params_to_instance(params_row, color)

    def _hill_climb(self, shape: Shape, iterations: int) -> Shape:
        """Parallel hill climb : à chaque étape on score [best, *K mutations]
        en une passe batch. L'argmin renvoie 0 si aucune mutation ne bat,
        sinon l'index de la gagnante."""
        K = 64
        steps = max(1, iterations // K)
        cls = shape.__class__
        best_params = shape.to_params_row()  # numpy 1D
        best_color = shape.color
        baseline = precompute_baseline(self.canvas, self.target, self.alpha_mask, xp=self.xp)
        no_improve = 0
        for _ in range(steps):
            if self._stop:
                break
            mut_params = cls.mutate_batch(
                best_params, self.rng, self.w, self.h, K, xp=self.xp,
            )
            best_xp = self.xp.asarray(best_params[None, :])
            all_params = self.xp.concatenate([best_xp, mut_params], axis=0)
            masks = cls.rasterize_batch(all_params, self.w, self.h, xp=self.xp)
            scores, colors = score_masks_batch(
                masks, self.canvas, self.target, self.alpha_mask,
                self._alpha_init, xp=self.xp, baseline=baseline,
            )
            idx = int(scores.argmin().get()) if hasattr(scores, 'get') else int(scores.argmin())
            if idx == 0:
                no_improve += 1
                if no_improve >= max(1, steps // 4):
                    break
                continue
            no_improve = 0
            best_params = to_cpu(all_params[idx])
            best_color = tuple(int(c) for c in to_cpu(colors[idx]))
        return cls.params_to_instance(best_params, best_color)

    def run(self) -> Iterable[EngineEvent]:
        p = self.profile
        types = [t for t in p.shape_types if t]
        if not types:
            types = ["rotated_ellipse"]
        save_at = set(p.save_at)
        try:
            consecutive_skips = 0
            MAX_CONSECUTIVE_SKIPS = 80
            while len(self.shapes) < p.stop_at and not self._stop:
                candidate = self._best_of_random(types, max(1, p.random_samples))
                refined = self._hill_climb(candidate, max(1, p.mutated_samples))
                if self.alpha_mask is not None:
                    sticker_attempts = 0
                    while sticker_attempts < 5:
                        rscore, _ = score_shape(refined, self.canvas, self.target, self.alpha_mask, xp=self.xp)
                        if rscore != float("inf"):
                            break
                        candidate = self._best_of_random(types, max(1, p.random_samples))
                        refined = self._hill_climb(candidate, max(1, p.mutated_samples))
                        sticker_attempts += 1
                    else:
                        consecutive_skips += 1
                        if consecutive_skips >= MAX_CONSECUTIVE_SKIPS:
                            yield EngineEvent(
                                kind="done",
                                shape_count=len(self.shapes),
                                rms=self.rms,
                                canvas=to_cpu(self.canvas.copy()),
                                message=(
                                    f"Arrêt anticipé à {len(self.shapes)} formes — "
                                    "plus aucune forme ne tient dans la zone opaque."
                                ),
                            )
                            return
                        continue
                    consecutive_skips = 0
                new_canvas, new_rms = composite(self.canvas, refined, self.target, self.alpha_mask, xp=self.xp)
                self.canvas = new_canvas
                self.rms = new_rms
                self.shapes.append(refined)
                count = len(self.shapes)

                yield EngineEvent(kind="shape_committed", shape_count=count, rms=self.rms)

                if p.preview_every and (count % p.preview_every == 0):
                    yield EngineEvent(kind="preview", shape_count=count, rms=self.rms, canvas=to_cpu(self.canvas.copy()))

                if count in save_at or (p.save_every and count % p.save_every == 0):
                    yield EngineEvent(kind="checkpoint", shape_count=count, rms=self.rms)

            yield EngineEvent(kind="done", shape_count=len(self.shapes), rms=self.rms, canvas=to_cpu(self.canvas.copy()))
        except Exception as exc:
            yield EngineEvent(kind="error", message=f"{type(exc).__name__}: {exc}")
