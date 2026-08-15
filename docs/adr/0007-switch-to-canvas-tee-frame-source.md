# ADR 0007 — Switch to the Canvas tee frame source; readback abandoned

**Status:** Accepted — implemented and verified on real hardware: dashboard
loads, mirror image renders correctly (matches the physical Launcher home
menu pixel-for-pixel, no scrambling), device stays up under real
browser/WebSocket traffic (2 clients, 24+ frames sent, no reboot).
**Deciders:** firmware owner
**Related:** Supersedes this repo's own ADR 0002 (readback for v1, tee
deferred). cardputer-adv-mirror ADR 0039 (`SpiReadbackFrameSource`, the
approach this replaces), ADR 0042 (heap guard around `binaryAll()`), ADR
0043 (removed a dead 64.8 KB buffer) — both on the cardputer-adv-mirror side,
both required to get this stable on real, PSRAM-less hardware.

## Context

ADR 0002 chose GRAM readback (`SpiReadbackFrameSource`) for v1 and
deliberately deferred the Canvas tee, reasoning that `USE_CANVAS` was
upstream-experimental ("testing purpose") and readback was the
already-proven path.

On real hardware, `SpiReadbackFrameSource::begin()` failed outright:
`spi_bus_add_device()` returned `ESP_ERR_INVALID_STATE`. Traced to real
ESP-IDF 5.5.4 source (not assumed): `spi_bus_add_device()` requires a prior
`spi_bus_initialize()` call for that host, and `esp32-hal-spi.c`
(arduino-esp32 3.x's own SPI implementation, which is what Launcher's
`Arduino_HWSPI` sits on) never calls it — it drives the peripheral directly
through lower-level HAL/LL calls instead. This is not a config bug (pin
numbers, host selection, and the later-discovered missing `clock_source`
field were all fixed and none of it helped) — it's arduino-esp32 3.x
bypassing the bus-registration bookkeeping the standard ESP-IDF SPI driver
depends on. Readback cannot attach to this bus on this host, full stop.

cardputer-adv-mirror ADR 0041 already decoupled "frame source failed" from
"server is down," so this failure alone did not block shipping remote
keyboard/dashboard access — but the mirror image itself stayed broken with
no readback alternative available on this host.

## Decision

Switch `LauncherAdapter::frameSource()` to `LauncherCanvasFrameSource`,
reading pixels from `Arduino_Canvas::getFramebuffer()` (a RAM buffer) instead
of the display SPI bus. This requires flipping `src/tft.h`'s `USE_CANVAS`
build flag on for the `m5stack-cardputer` env — the exact upstream switch
ADR 0002 flagged as experimental and declined to flip. It gets flipped now
because the alternative (readback) is not a "lower fps, more tearing"
tradeoff on this host, it is completely non-functional.

Two things had to be gotten right, verified against real source rather than
assumed:

1. **Display safety.** Before touching the frame-source side, `USE_CANVAS`
   was enabled alone and the device was confirmed still rendering the
   Launcher menu correctly — the canvas write path had to be proven safe
   before anything read from it.
2. **Framebuffer geometry.** `Arduino_Canvas::writePixelPreclipped()`'s
   rotation==1 case (`lib_modules/Arduino_GFX/src/canvas/Arduino_Canvas.cpp`)
   stores the buffer column-major with Y flipped within each column, not
   row-major in the logical (post-rotation) view. `fetchTile()` inverts that
   exact formula (`fb[x * _fbHeight + (maxY - y)]`). Getting this wrong
   doesn't crash — it silently scrambles the mirror image — so it was
   verified visually against the real device (a real screenshot of the
   dashboard showing the correct, unscrambled Launcher home menu), not just
   reasoned about from source.

Two crashes surfaced and were fixed on the cardputer-adv-mirror side before
this was stable (see that repo's ADR 0042 and ADR 0043): this hardware has
no PSRAM, and the combined footprint of the canvas framebuffer plus a
64.8 KB buffer in `Mirror` that turned out to be dead code (written, never
read) left too little internal SRAM headroom for `AsyncWebServer` to survive
a normal HTTP request under `-fno-exceptions`. Both fixes are core-side and
apply to any PSRAM-less host, not just this one.

## Consequences

**Positive**

- The display mirror actually works now, verified end-to-end on real
  hardware: page loads, image is correct, device stays up under real
  traffic.
- No fps ceiling from a read clock, and no bus contention with Launcher's
  own draws — the tee reads a RAM buffer Launcher already maintains.
- `SpiReadbackFrameSource`'s architectural incompatibility with
  arduino-esp32 3.x is now moot for this project; nothing here depends on
  fixing it.

**Negative**

- Depends on an upstream-experimental Launcher build path (`USE_CANVAS`,
  "testing purpose"). If Launcher's own Canvas implementation changes its
  buffer layout, `fetchTile()`'s geometry formula silently breaks (wrong
  pixels, not a crash) until re-verified.
- Adds the canvas framebuffer's own RAM cost (64.8 KB) on top of Launcher's
  baseline — this is exactly what made the PSRAM-less memory budget tight
  enough to crash before cardputer-adv-mirror ADR 0042/0043.

**Neutral**

- `SpiReadbackFrameSource` and its remaining `_debugSpiErr`/
  `debugSpiAddDeviceErr()` diagnostic are left as-is in cardputer-adv-mirror
  for other hosts where the standard ESP-IDF SPI driver bus registration
  does apply — this ADR only changes what launcher-adv-mirror's own adapter
  constructs.

## Alternatives considered

- **Fix `SpiReadbackFrameSource` to work around arduino-esp32 3.x's bus
  bypass** (e.g. call `spi_bus_initialize()` ourselves before Launcher's own
  SPI use). Rejected: would mean managing SPI bus lifecycle state that
  belongs to Launcher/arduino-esp32, fighting the framework instead of using
  what it already exposes (`Arduino_Canvas`) for exactly this purpose.
- **Ship with no working display mirror, keep only remote control.**
  Rejected now that Canvas has been proven both safe to enable and correct
  to read from — there's no remaining reason to ship the degraded version
  once the working one is verified.
