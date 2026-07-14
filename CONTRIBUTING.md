**🇬🇧 English** | [🇫🇷 Français](docs/fr/CONTRIBUTING.md)

# Contributing

Forza Painter 6 is maintained by a single person as a hobby project. Issues and pull requests are welcome, but review may take a while.

## Reporting a bug

Open a GitHub issue with:
- Your Forza Horizon 6 build number (relevant for memory injection issues)
- Steps to reproduce
- For crashes: console output from a `make debug` build (see [native/README.md](native/README.md)) if possible

## Building from source

See [native/README.md](native/README.md) for the native C build (MSYS2 UCRT64) and [legacy/README.md](legacy/README.md) for the archived Python version.

## Code conventions

- Native app (`native/`): C11, no runtime dependency beyond what MSYS2/UCRT64 provides. Keep the single-`.exe`, zero-DLL constraint.
- Legacy app (`legacy/`): kept as-is for reference; not actively developed.
- Document notable changes in [CHANGELOG.md](CHANGELOG.md).

## Pull requests

- Keep changes focused and scoped to one topic.
- Test on Windows before submitting (this is a Windows-only project: Win32 GUI + `WriteProcessMemory`).
- Small fixes (typos, docs) can go straight to a PR; for larger changes, open an issue first to discuss the approach.
