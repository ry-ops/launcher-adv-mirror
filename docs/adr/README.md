# ADR Index — Launcher ADV Mirror

Architecture Decision Records for integrating
[cardputer-adv-mirror](https://github.com/ry-ops/cardputer-adv-mirror) (a
browser display mirror + remote control library) into
[Launcher](https://github.com/bmorcelli/Launcher) on the M5Stack Cardputer ADV.

This repo is a fork of Launcher. It never modifies Launcher's own source
beyond `adapters/launcher/`, and never modifies the mirror library at all —
see ADR 0001. Changes to the mirror library itself belong in
`cardputer-adv-mirror`'s own ADR sequence (currently 0038+).

| ADR | Title | Status |
|-----|-------|--------|
| [0001](0001-adapter-architecture.md) | Adapter architecture: this fork consumes CardputerMirror as an unmodified library | Proposed |
| [0002](0002-readback-frame-source.md) | Frame source for v1: GRAM readback, Canvas tee deferred | Proposed |
| [0003](0003-input-injection.md) | Input injection: target Launcher's real `launcherInputLock`/`KeyStroke` | Proposed |
| [0004](0004-spi-bus-lock.md) | SPI bus lock: why `nullptr` is correct today, and what would invalidate it | Proposed |
| [0005](0005-dependency-management.md) | Dependency management: CardputerMirror as a submodule, Launcher as a tracked fork | Proposed |
| [0006](0006-build-integration-fixes.md) | Build integration fixes: `lib_ignore`, WDT config, partition size | **Accepted — implemented** |

## Hardware facts these ADRs rest on

Same physical device as cardputer-adv-mirror's own ADRs — verified against a
real clone of `bmorcelli/Launcher`, not assumed:

| Property | Value | Source |
|---|---|---|
| Board | M5Stack Cardputer ADV, `boards/m5stack-cardputer/` in Launcher | Launcher repo |
| Panel | ST7789, 135x240 | `boards/m5stack-cardputer/platformio.ini` |
| Pins | MOSI 35, SCLK 36, DC 34, CS 37, RST 33, BL 38 | same file — matches cardputer-adv-mirror exactly |
| Keyboard | TCA8418 I2C, addr 0x34, INT GPIO11, polling (interrupt pin not wired in hardware) | `boards/m5stack-cardputer/interface.cpp` |
| Canvas tee path | `USE_CANVAS` exists in `src/tft.h` but is `#define`d off | `src/tft.h` |
| Input lock | `launcherInputLock()`/`launcherInputUnlock()`, recursive mutex over shared `KeyStroke` | `src/mykeyboard.cpp:129` |
| Render task | None — drawing happens on `loopTask`; only `InputHandler` and `SerialConsole` run as separate tasks | `src/main.cpp` |
| Key matrix | `_key_value_map[4][14]` — byte-identical to M5Cardputer's own copy (diffed directly) | `lib/utility/Keyboard.h:41` |
