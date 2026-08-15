# ADR 0002 — Frame source for v1: GRAM readback, Canvas tee deferred

**Status:** Accepted — implemented (`SpiReadbackFrameSource`, cardputer-adv-mirror
ADR 0039). Builds clean; correctness on real hardware still unverified — see
that ADR.
**Deciders:** firmware owner
**Related:** ADR 0001 (adapter architecture); cardputer-adv-mirror ADR 0001
(tee) / 0002 (readback, accepted there); cardputer-adv-mirror ADR 0038
(`IHostAdapter::frameSource()`, the seam this ADR fills).

## Context

Two frame sources exist in principle:

- **Readback** — `cardputer-adv-mirror`'s `ReadbackFrameSource`, already
  built and accepted there (ADR 0002). Polls the panel's GRAM over the
  existing SPI bus; needs nothing from the host beyond the bus itself.
- **Tee** — intercept the host's own draw calls at the framebuffer. Higher
  fps ceiling, no read-clock cap, but requires the host to expose a
  framebuffer to tee from.

Checked against Launcher directly, not assumed:

- `src/tft.h` has a `USE_CANVAS` build mode wrapping `Arduino_GFX`'s
  `Arduino_Canvas`, which does expose `getFramebuffer()` — exactly the tee
  point this would need. It is `#define`d **off** in the checked-out source
  ("testing purpose"). The `m5stack-cardputer` env builds `ARD_TFT_BASE` as
  the raw `_TFT_DRV` (ST7789 driver) with no intervening framebuffer.
- Panel pins in `boards/m5stack-cardputer/platformio.ini`
  (`TFT_MOSI=35 TFT_SCLK=36 TFT_DC=34 TFT_CS=37 TFT_RST=33`, 135x240) are
  identical to the pins `cardputer-adv-mirror`'s own ADRs verified for this
  exact panel. `ReadbackFrameSource` needs no per-host tuning to work here.

So today, only one of the two options actually exists to choose.

## Decision

**v1 ships readback only.** `LauncherAdapter::frameSource()` owns a
`cmirror::ReadbackFrameSource` instance (member, constructed in the adapter,
returned by reference — see cardputer-adv-mirror ADR 0038) and returns it
unmodified. No Launcher file needs to change for this half of the adapter:
readback reads the panel's GRAM directly over SPI and does not care what
draw library sits above it.

`selfTest()` runs at adapter `begin()`, same as the standalone example in
cardputer-adv-mirror's own `src/main.cpp`, and its score should be surfaced
through whatever status/serial output this project ends up with — the
number that answers "is this frame trustworthy" before any pixel is shown.

**Tee is future work**, gated on two things happening upstream in Launcher,
neither of which this project should force: `USE_CANVAS` becoming the
default (or at least a proven, non-"testing purpose" path) for the
`m5stack-cardputer` env, and confirming `Arduino_Canvas::getFramebuffer()`'s
layout matches what a `TeeFrameSource` would expect (RGB565, screen-major,
matching `kScreenW`/`kScreenH` in `CardputerMirror.h`).

## Consequences

**Positive**

- Zero Launcher edits for video. The entire frame-source half of the
  integration is "construct a class that already exists and works."
- De-risked: this is the same frame source cardputer-adv-mirror's own ADR
  0002 already validated with a boot self-test, on the same panel.

**Negative**

- Capped at readback's ~20 fps ceiling (cardputer-adv-mirror ADR 0002),
  lower under Launcher's own draw load, since reads contend with Launcher's
  writes on the same SPI bus.
- Tearing and CRC-sampling blind spots (cardputer-adv-mirror ADR 0002's
  known limits) apply here unchanged — a tile drawn and reverted between
  scans is invisible to the mirror.

**Neutral**

- If `USE_CANVAS` is ever enabled upstream in Launcher for unrelated
  reasons, this ADR should be revisited — the tee option would then cost
  nothing extra to adopt.

## Alternatives considered

- **Enable `USE_CANVAS` ourselves and ship the tee in v1.** Rejected:
  `USE_CANVAS` is explicitly marked experimental upstream ("testing
  purpose"); shipping on an untested upstream path in someone else's
  firmware is a worse bet than the readback path this project's own library
  already proved out.
- **Wait for a tee and skip readback entirely.** Rejected: blocks on an
  upstream decision this project doesn't control, for a fps improvement
  that isn't required to prove the integration works at all.
