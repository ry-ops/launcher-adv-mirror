# ADR 0006 — Build integration fixes: `lib_ignore`, WDT config, partition size

**Status:** Accepted — implemented. Verified: `pio run -e m5stack-cardputer`
succeeds end to end, `Launcher-m5stack-cardputer.bin` (1,526,944 B) built,
92.9% of the resized `app0` partition (real headroom, not razor-thin).
**Deciders:** firmware owner
**Related:** ADR 0001 (adapter architecture), ADR 0005 (dependency
management — the submodule/`lib_ignore` interaction below is the concrete
mechanism that ADR promised); cardputer-adv-mirror ADR 0040 (the
`esp32async/ESPAsyncWebServer` switch this build depends on).

## Context

Getting `env:m5stack-cardputer` to actually build (not just cardputer-adv-mirror's
own `env:cardputer-adv`) surfaced three real, independent problems — none of
them bugs in the mirror or the adapter code, all specific to Launcher's
build environment. Recorded here because each one is the kind of thing that
looks like a code bug from the error message alone, and isn't.

## Decision

**1. `lib_ignore = CardputerMirror`** in
`boards/m5stack-cardputer/platformio.ini`.

The root `platformio.ini` sets `lib_extra_dirs = lib_modules` for every env,
which makes PlatformIO's Library Dependency Finder auto-discover *every*
subdirectory of `lib_modules/` as a candidate library — including the
`CardputerMirror` submodule's own root, using *its* `src/` as the inferred
library source. That's cardputer-adv-mirror's standalone example app
(`main.cpp`, `keyinject.cpp`, `menu.cpp`), not the library. The real library
is already pulled in explicitly via `build_src_filter` +
`-Ilib_modules/CardputerMirror/lib/CardputerMirror` (ADR 0001) — without
`lib_ignore`, both paths compiled at once, and the standalone example's own
`main()`/keyinject/menu collided with Launcher's. Same mechanism this file
already uses to exclude `SensorLib`/`XPowersLib` for this board.

**2. `-DCONFIG_ASYNC_TCP_USE_WDT=0`** in the same file's `build_flags`.

`AsyncTCP` (pulled in via `esp32async/ESPAsyncWebServer`, cardputer-adv-mirror
ADR 0040) defaults to assuming the ESP-IDF task watchdog is available
(`CONFIG_ASYNC_TCP_USE_WDT=1` unless told otherwise) and then needs
`CONFIG_ESP_TASK_WDT_TIMEOUT_S` from it. Checked directly: Launcher's custom
`framework-arduinoespressif32-libs` build (`bmorcelli/myLibBuilder`) has no
`CONFIG_ESP_TASK_WDT*` entries anywhere in its `sdkconfig.h` — the task
watchdog component isn't compiled in at all. `CONFIG_ASYNC_TCP_USE_WDT=0` is
AsyncTCP's own supported escape hatch for exactly this case (the same one
its `LIBRETINY` platform branch uses internally), not a workaround.

**3. `app0` partition resized from `0x150000` to `0x180000`** in
`support_files/custom_8Mb.csv`.

Adding the mirror (ESPAsyncWebServer + AsyncTCP + the mirror core) pushed
the built firmware to 1,461,007 B, over the previous `app0` slot
(1,376,256 B) — and over the existing "debug" alternative partition table
(`custom_8Mb2.csv`, `0x160000` = 1,441,792 B) too, by about 19 KB. Rather
than reach for that alternative and still be short, `app0` was resized
directly to `0x180000` (1,572,864 B, ~112 KB of headroom over the measured
size). This board's 8 MB flash has ample room — the whole partition table
still uses only ~1.6 MB of it afterward. `coredump`'s offset moved forward
to follow (`0x190000`); nothing else in the table changed.

## Consequences

**Positive**

- All three are narrow, well-understood, and each fixes exactly one
  measured failure — no speculative headroom padding beyond what was
  actually observed.
- `lib_ignore` and the WDT flag cost nothing at runtime; the partition
  resize costs flash space this board has plenty of.

**Negative**

- The partition table change affects *every* build of this board target,
  not just ones that end up using the mirror actively — anyone building
  `env:m5stack-cardputer` now gets the larger `app0` regardless. Acceptable
  here since the mirror is meant to always be present for this board once
  merged, not an optional feature toggle (ADR 0001).
- `CONFIG_ASYNC_TCP_USE_WDT=0` means AsyncTCP's internal service task has no
  watchdog supervision on this build. Matches Launcher's own framework
  choice (it has no task-watchdog component at all here) rather than
  introducing a new gap.

**Neutral**

- None of these three needed a change to cardputer-adv-mirror itself, or to
  any Launcher file besides this board's own `platformio.ini` and partition
  CSV — both already board-scoped, board-owned files.

## Alternatives considered

- **Switch to `custom_8Mb2.csv` instead of resizing `custom_8Mb.csv`
  directly.** Rejected: it's 0x160000, still ~19 KB short of the measured
  firmware size — would have needed resizing anyway, and switching the
  active partition file is a bigger, less targeted change than adjusting
  one field in the one already in use.
- **Vendor a patched `AsyncTCP` instead of the `CONFIG_ASYNC_TCP_USE_WDT=0`
  flag.** Rejected: the flag is the library's own documented, supported
  mechanism for builds without WDT support — nothing to patch.
