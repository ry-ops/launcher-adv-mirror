# ADR 0001 — Adapter architecture: this fork consumes CardputerMirror as an unmodified library

**Status:** Accepted — implemented. `env:m5stack-cardputer` builds and
packages end to end (see ADR 0006 for the three build-environment fixes that
took). **Verified on real hardware:** the dashboard page serves correctly at
`http://<device-ip>/`, and remote key injection moves the on-screen menu via
a real WebSocket connection (ADR 0003). The display mirror itself is still
broken (`SpiReadbackFrameSource` can't attach to the display bus on this
host yet) but ADR 0041 (in cardputer-adv-mirror) means that no longer takes
the rest of the integration down with it.
**Deciders:** firmware owner
**Related:** cardputer-adv-mirror ADR 0038 (adapter-driven `begin()`, defines the
contract this ADR consumes); cardputer-adv-mirror ADR 0001/0002 (frame source
options), ADR 0004/0017 (input injection design). This ADR formalizes, for a
specific host, the plan sketched informally as a standalone "adapter
architecture" note before this repo existed.

## Context

[cardputer-adv-mirror](https://github.com/ry-ops/cardputer-adv-mirror) is a
browser display mirror + remote control library for the M5Stack Cardputer ADV.
It already states its own boundary in its README: *"the library owns no
policy of its own."* [Launcher](https://github.com/bmorcelli/Launcher) is a
multi-board ESP32 firmware launcher that already runs on the Cardputer ADV
(`boards/m5stack-cardputer/`). This repo's purpose is to put the mirror inside
Launcher without turning either project into a fork-and-diverge situation.

Verified against a real clone of `bmorcelli/Launcher` (not assumed):

- The ADV's panel pins in Launcher's `boards/m5stack-cardputer/platformio.ini`
  (`TFT_MOSI=35 TFT_SCLK=36 TFT_DC=34 TFT_CS=37 TFT_RST=33`, ST7789 135x240)
  match cardputer-adv-mirror's own verified hardware facts exactly — the same
  physical panel, so GRAM readback needs no adaptation.
- Launcher's `src/tft.h` has a `USE_CANVAS` mode wrapping `Arduino_Canvas`
  with `getFramebuffer()` — a ready-made tee point — but it is currently
  `#define`d off ("testing purpose"). Live builds draw straight through the
  ST7789 driver. No framebuffer exists to tee from today.
- Launcher already has a real input-lock: `launcherInputLock()` /
  `launcherInputUnlock()` (`src/mykeyboard.cpp:129`), a recursive mutex
  guarding a shared `KeyStroke` global (plus `NextPress`, `PrevPress`,
  `SelPress`, `EscPress`, `AnyKeyPress`). It's written by a dedicated
  `taskInputHandler` FreeRTOS task (`src/main.cpp`, priority 2, same priority
  as `loopTask`) and read by the main loop through `_getKeyPress()`.
- Rendering has no dedicated task. Besides `loopTask`, only `InputHandler`
  and `SerialConsole` run as separate FreeRTOS tasks, and neither touches the
  display SPI bus. All `tft->...` draw calls happen on `loopTask` itself.
- Launcher already vendors its own dependencies (`ArduinoJson`, `Arduino_GFX`,
  `SensorLib`, `XPowersLib`) as git submodules under `lib_modules/`, and
  already has a per-board adapter convention (`boards/<board>/interface.cpp`
  + a `platformio.ini` fragment) for host-specific hooks like GPIO setup and
  keyboard handling.
- `boards/m5stack-cardputer/CardputerADV.md` (pre-existing in Launcher,
  written by a different contributor) documents a `-e m5stack-cardputer-adv`
  PlatformIO environment that does not exist — ADV support is folded into the
  single `m5stack-cardputer` env via runtime TCA8418 probing in
  `interface.cpp`. Noted so this ADR isn't built on that doc's assumptions.

## Decision

This repo is a **fork of `bmorcelli/Launcher`**, tracking an `upstream` remote
so Launcher's own fixes keep merging in. It adds exactly two things on top of
upstream Launcher:

1. **`lib_modules/CardputerMirror`** — a **git submodule** pointing at
   `ry-ops/cardputer-adv-mirror`. Never hand-edited in this repo. A submodule
   makes that a structural guarantee, not a convention: there is no file here
   to accidentally edit. Bumping the mirror to a new version is a submodule
   pointer update, not a merge.
2. **`adapters/launcher/LauncherAdapter.{h,cpp}`** — one new file implementing
   the `IHostAdapter` contract (cardputer-adv-mirror ADR 0038) against
   Launcher's real internals:
   - `frameSource()` → the library's own `ReadbackFrameSource`, unmodified.
   - `inputSink()` → wraps `launcherInputLock()` / `launcherInputUnlock()`
     and writes into `KeyStroke` / `NextPress` / `PrevPress` / `SelPress` /
     `EscPress` / `AnyKeyPress` — the same globals `taskInputHandler` already
     writes, so the mirror looks like just another key source to the rest of
     Launcher.
   - `busLock()` → `nullptr` (see ADR 0004 — no separate render task exists
     today, so there is nothing to lock against).

Plus two calls into Launcher's own `setup()`/`loop()` (or
`boards/m5stack-cardputer/interface.cpp`) selecting and driving the adapter —
the same "two lines" integration cardputer-adv-mirror's README already
promises any host.

`adapters/launcher/` (a top-level directory, not nested inside
`boards/m5stack-cardputer/`) deliberately mirrors the layout cardputer-adv-mirror's
own planning notes proposed, so a second host (Bruce, nemo, standalone) is a
sibling directory, not a restructure.

## Consequences

**Positive**

- The mirror library is never firmware-specific and never diverges from
  upstream `cardputer-adv-mirror` — updates are a one-line submodule bump.
- Launcher is never diverged either — `adapters/launcher/` is pure addition,
  so `git fetch upstream && git merge upstream/main` should stay conflict-free
  in the common case.
- The only code that knows both projects exist is `LauncherAdapter.{h,cpp}` —
  the entire integration surface is one small, readable file.
- Matches Launcher's own existing conventions (`lib_modules/*` submodules,
  per-board adapter files), so it isn't a foreign pattern grafted on.

**Negative**

- Two upstreams to track (`bmorcelli/Launcher`, `ry-ops/cardputer-adv-mirror`),
  each on its own release cadence; a breaking change in either can require
  adapter updates before the next merge is trivial.
- `busLock() -> nullptr` is correct only as long as Launcher's rendering stays
  on `loopTask`. If Launcher ever moves drawing to its own task (see ADR
  0004), this adapter silently becomes unsafe until updated.
- The tee frame source is unavailable until Launcher's `USE_CANVAS` path is
  enabled and proven (see ADR 0002) — v1 is capped at readback's ~20 fps
  ceiling, not the tee's higher rate.

**Neutral**

- `adapters/launcher/` sets a directory convention this repo will only prove
  out once (Launcher is the only host today); worth revisiting if a second
  host adapter is ever added here instead of in its own fork.

## Alternatives considered

- **Vendor the mirror's source directly into this fork.** Rejected: turns
  every mirror update into a manual diff-and-reapply instead of a submodule
  bump, and removes the structural guarantee that Launcher-specific code
  can't leak into the library.
- **`git subtree` instead of a submodule.** Considered for real merge history
  in one repo instead of a pointer. Rejected for v1: heavier tooling for no
  benefit while the mirror stays a clean, unmodified dependency; revisit only
  if this fork ever needs Launcher-specific patches to the mirror's own files.
- **Put the adapter inside `boards/m5stack-cardputer/interface.cpp`
  directly**, matching Launcher's existing per-board pattern exactly instead
  of a new top-level directory. Rejected: couples the mirror's integration to
  one specific board file, and the ADR this generalizes explicitly wants
  adapters to be a first-class, swappable unit for future hosts.
