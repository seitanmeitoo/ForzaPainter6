import os
import sys

def _register_cuda_dll_dirs() -> None:
    """On Windows + Python 3.8+, PATH is not searched for DLLs (SetDefaultDllDirectories).
    CuPy's cuda-pathfinder falls back to CUDA_PATH but only if the env var is inherited.
    When the app is launched via double-click or an IDE that strips env vars, nvrtc is
    not found and CuPy raises 'Failure finding nvrtc*.dll'.  We register the CUDA bin
    directories explicitly so they're always in the DLL search path."""
    if sys.platform != "win32":
        return
    cuda_path = os.environ.get("CUDA_PATH") or os.environ.get("CUDA_HOME")
    if not cuda_path:
        return
    for sub in ("bin\\x64", "bin"):
        d = os.path.join(cuda_path, sub)
        if os.path.isdir(d):
            try:
                os.add_dll_directory(d)
            except OSError:
                pass

_register_cuda_dll_dirs()

from gui import App


if __name__ == "__main__":
    App().run()
