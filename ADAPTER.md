# Launcher ADV Mirror — integration notes

This repo is a fork of [Launcher](https://github.com/bmorcelli/Launcher). Its
`README.md` is upstream's own — don't edit it here, to keep merges from
`upstream` clean. This file documents the one thing this fork adds:
[cardputer-adv-mirror](https://github.com/ry-ops/cardputer-adv-mirror)
(browser display mirror + remote control for the M5Stack Cardputer ADV),
integrated without modifying either project's own source beyond one small,
named exception.

What this fork adds on top of upstream Launcher:

- `lib_modules/CardputerMirror` — a git submodule of the mirror library,
  never hand-edited here (see `docs/adr/0001`, `0005`).
- `adapters/launcher/` — one adapter implementing the mirror's
  `IHostAdapter` contract against Launcher's real internals: GRAM readback
  for video (`docs/adr/0002`), `launcherInputLock`/`KeyStroke` for input
  (`docs/adr/0003`), no bus lock needed today (`docs/adr/0004`).
- A small, named extraction inside `boards/m5stack-cardputer/interface.cpp`
  (`applyMatrixKeyEvent()`) so the remote input path and the real TCA8418
  path share one implementation of Launcher's key semantics instead of two
  — the one deliberate exception to "pure addition" (`docs/adr/0003`).

See [`docs/adr/`](docs/adr/README.md) for the full reasoning, the hardware
facts everything rests on, and what's still open.

## Remotes

- `origin` → `ry-ops/launcher-adv-mirror` (this fork)
- `upstream` → `bmorcelli/Launcher` — `git fetch upstream && git merge
  upstream/main` to pull in Launcher's own updates (`docs/adr/0005`).
