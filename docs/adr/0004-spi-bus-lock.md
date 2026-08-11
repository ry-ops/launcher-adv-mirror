# ADR 0004 — SPI bus lock: why `nullptr` is correct today, and what would invalidate it

**Status:** Proposed
**Deciders:** firmware owner
**Related:** ADR 0001 (adapter architecture); ADR 0002 (readback, the thing
being locked against); cardputer-adv-mirror ADR 0002 (why the scanner runs on
the main task, not a background task — the same hazard this ADR is about);
cardputer-adv-mirror ADR 0038 (`IHostAdapter::busLock()`, the seam this fills).

## Context

cardputer-adv-mirror ADR 0002 establishes the load-bearing rule for readback:
**LGFX/M5GFX-style panel drivers are not thread-safe.** A background task
calling `readRect()` while another task is mid-draw interleaves two SPI
transactions on the same bus and corrupts pixels or faults. cardputer-adv-mirror
avoids a mutex entirely by running its scanner from `loop()`, inheriting
whatever serialization the application's own single-threaded `loop()` already
gives it for free.

Launcher is not single-threaded. Checked directly against Launcher's
`src/main.cpp`: `setup()` creates `taskInputHandler` (priority 2, same as
`loopTask`) and `taskSerialConsole` (priority 1) as separate FreeRTOS tasks.
The question this ADR has to answer is whether either of those — or anything
else — ever touches the display SPI bus. If something does, readback run
from `loop()` needs to take a real mutex around it, matching what
`IHostAdapter::busLock()` (cardputer-adv-mirror ADR 0038) exists for.

Checked: neither `taskInputHandler` nor `taskSerialConsole` call into `tft`
or any draw path — `taskInputHandler` only runs `InputHandler()` (keyboard
I2C + globals) and `checkPowerSaveTime()`; `taskSerialConsole` handles the
serial command interface. All `tft->...` calls found in Launcher's `src/`
happen from functions reached through `loop()` on `loopTask` itself. There is
no dedicated render task today.

## Decision

`LauncherAdapter::busLock()` returns `nullptr`. Given `IHostAdapter::begin()`
also arranges for `CardputerMirror.update()` (via whatever this project's
equivalent driving call is) to run from Launcher's own `loop()` — the same
task that does all of Launcher's drawing — readback and Launcher's draw
calls are already serialized by construction, the same way cardputer-adv-mirror's
own standalone example gets serialization for free. There is nothing to lock.

**This is a fact about Launcher's current task structure, not a permanent
property of it.** If Launcher ever moves rendering to its own task — for
performance, for a UI framework migration, for anything — this becomes
silently wrong: readback would start racing that task's SPI writes with no
mutex protecting either side, reproducing exactly the corruption/fault mode
cardputer-adv-mirror ADR 0002 describes.

There is no automated way to catch this from inside `LauncherAdapter` — it
can't introspect "does anything else touch this bus." The mitigation is
procedural: **re-verify this ADR's premise on every upstream Launcher merge**
(ADR 0005) by checking whether `xTaskCreate` calls in `src/main.cpp` (or
elsewhere) grew a new task that reaches a `tft->...` call. Grep, not
guesswork — the same way this ADR's premise was established.

## Consequences

**Positive**

- Simplest possible implementation: no mutex, no take/give overhead, nothing
  to get wrong in v1.
- Matches the reasoning cardputer-adv-mirror's own ADR 0002 already validated
  for the standalone case — this isn't a new risk, it's the same one,
  re-verified for a second host.

**Negative**

- A silent trap: nothing fails loudly the moment this stops being true. The
  failure mode is display corruption or a bus fault that shows up as "the
  mirror broke after updating Launcher," which is a bad debugging starting
  point if this ADR's premise isn't the first thing checked.
- Puts a manual verification step in the upstream-merge process (ADR 0005)
  that's easy to skip under time pressure — exactly when it matters most,
  since a new task is most likely to arrive in a larger upstream refactor.

**Neutral**

- If Launcher ever does add a render task, the fix is well-understood and
  small: return that task's real mutex from `busLock()` instead of
  `nullptr`, and have `Mirror`'s adapter-driven path (cardputer-adv-mirror
  ADR 0038) take/give it around `IFrameSource` calls. No architecture change,
  just filling in a value that's `nullptr` today.

## Alternatives considered

- **Take a mutex unconditionally, even though nothing needs it today.**
  Rejected: pure overhead with no correctness benefit while the premise
  holds, and it papers over the real question ("does Launcher's task
  structure make this safe?") instead of answering it.
- **Have the adapter create and hold its own mutex, and require Launcher's
  draw calls to take it too.** Rejected: requires editing every Launcher
  draw site to take a mirror-specific lock, which is precisely the
  "non-invasive" property cardputer-adv-mirror ADR 0002 already rejected a
  version of for the exact same reason (there, it would have meant editing
  the *application's* draw sites; here it's Launcher's).
