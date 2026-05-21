"""Array-module abstraction so the same code runs on CPU (NumPy) or GPU (CuPy).

CuPy is an optional dependency. If `cupy` is not importable, GPU mode is
silently unavailable and everything stays on the CPU. The user installs it
separately via `pip install cupy-cuda12x` (or the variant matching their
CUDA toolkit) to enable GPU acceleration.

Usage:
    from shapegen.xp import get_xp, to_cpu, gpu_available
    xp = get_xp(use_gpu=True)              # cupy if available, else numpy
    arr = xp.zeros((512, 512), xp.uint8)   # lives on GPU when xp is cupy
    cpu_arr = to_cpu(arr)                  # convert back to numpy for PIL / Tk
"""
from __future__ import annotations

import numpy as _np

try:
    import cupy as _cp
    HAS_CUPY = True
except ImportError:
    _cp = None
    HAS_CUPY = False


def get_xp(use_gpu: bool):
    """Renvoie le module array (cupy ou numpy).

    Lève RuntimeError si use_gpu=True mais CuPy n'est pas installé.
    """
    if use_gpu:
        if not HAS_CUPY:
            raise RuntimeError(
                "Accélération GPU demandée mais CuPy n'est pas installé. "
                "Installez `cupy-cuda12x` (ou la variante adaptée à votre CUDA) "
                "puis relancez."
            )
        return _cp
    return _np


def to_cpu(arr):
    """Convertit un array en numpy.ndarray côté CPU.

    No-op pour un array NumPy. Pour un array CuPy, déclenche un D2H transfer.
    """
    if HAS_CUPY and isinstance(arr, _cp.ndarray):
        return _cp.asnumpy(arr)
    return _np.asarray(arr)


def gpu_available() -> bool:
    """Vrai si CuPy est installé ET qu'au moins un GPU NVIDIA est détecté."""
    if not HAS_CUPY:
        return False
    try:
        return _cp.cuda.runtime.getDeviceCount() > 0
    except Exception:
        return False


def gpu_device_name() -> str | None:
    """Nom du premier GPU détecté, ou None si indispo."""
    if not gpu_available():
        return None
    try:
        props = _cp.cuda.runtime.getDeviceProperties(0)
        name = props.get("name") if isinstance(props, dict) else props.name
        if isinstance(name, bytes):
            name = name.decode("utf-8", errors="replace")
        return name.strip()
    except Exception:
        return None
