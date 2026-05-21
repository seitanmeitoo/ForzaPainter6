from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable
import os
import random
import time
from concurrent.futures import ThreadPoolExecutor

import numpy as np

from shapegen.profile import Profile
from shapegen.scoring import composite, rms_error, score_shape, precompute_baseline
from shapegen.shapes import Shape, random_shape
from shapegen.xp import get_xp, to_cpu


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
            avg = self.target.reshape(-1, 3).mean(axis=0).astype(self.xp.uint8)
            self.canvas = self.xp.tile(avg, (self.h, self.w, 1)).astype(self.xp.uint8)
        self.shapes: list[Shape] = []
        self.rms = rms_error(self.canvas, self.target, self.alpha_mask, xp=self.xp)
        self.start_rms = self.rms
        self._stop = False
        seed = config.seed or int(time.time() * 1000) & 0xFFFFFFFF
        self.rng = random.Random(seed)
        # CuPy is not thread-safe across CPU threads; fall back to a single worker.
        if config.use_gpu:
            threads = 1
        else:
            threads = self.profile.max_threads or os.cpu_count() or 1
        self._executor = ThreadPoolExecutor(max_workers=max(1, threads))

    def request_stop(self) -> None:
        self._stop = True

    def _generate_candidate(self, types: list[str]) -> Shape:
        return random_shape(self.rng, self.w, self.h, types)

    def _best_of_random(self, types: list[str], n: int) -> Shape:
        candidates = [self._generate_candidate(types) for _ in range(n)]
        # Précompute once: every candidate scores against the same canvas, so
        # the O(H×W) global diff sum is shared. Cuts scoring cost by ~N for
        # the dominant term.
        baseline = precompute_baseline(self.canvas, self.target, self.alpha_mask, xp=self.xp)
        scored = list(self._executor.map(
            lambda s: (score_shape(s, self.canvas, self.target, self.alpha_mask, xp=self.xp, baseline=baseline), s),
            candidates,
        ))
        best = min(scored, key=lambda pair: pair[0][0])
        ((_rms, color), shape) = best
        shape.color = color
        return shape

    def _hill_climb(self, shape: Shape, iterations: int) -> Shape:
        baseline = precompute_baseline(self.canvas, self.target, self.alpha_mask, xp=self.xp)
        best = shape
        best_rms, best_color = score_shape(best, self.canvas, self.target, self.alpha_mask, xp=self.xp, baseline=baseline)
        best.color = best_color
        no_improve = 0
        for _ in range(iterations):
            if self._stop:
                break
            cand = best.mutate(self.rng, self.w, self.h)
            r, c = score_shape(cand, self.canvas, self.target, self.alpha_mask, xp=self.xp, baseline=baseline)
            if r < best_rms:
                best = cand
                best.color = c
                best_rms = r
                no_improve = 0
            else:
                no_improve += 1
                if no_improve >= max(20, iterations // 4):
                    break
        return best

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
        finally:
            self._executor.shutdown(wait=False)
