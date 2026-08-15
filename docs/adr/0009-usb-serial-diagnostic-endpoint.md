# ADR 0009 — `/diag` HTTP endpoint for USB serial health

**Status:** Accepted — implemented, builds clean. This is a diagnostic tool,
not a fix — the underlying USB CDC silence it's meant to help debug is still
unexplained. Real-hardware verification (does it actually report useful data
next time the link goes silent) still pending.
**Deciders:** firmware owner
**Related:** None directly, but motivated by a real incident this session:
serial went completely silent (no heartbeat, no response to any console
command) for an extended period, surviving a fresh reflash and multiple
device power cycles, with no way to see why from the host side.

## Context

During live testing, the USB serial link went silent — not just missing a
one-shot boot print (an already-known, separate ESP32-S3 native-USB-CDC
quirk), but zero bytes in either direction for many minutes, confirmed with
three independent host-side tools (`pyserial`, plain `cat` on the raw
device node, and `esptool`'s own read path). Notably, `esptool upload`
*succeeded* during this window — the bootloader-level USB Serial/JTAG
channel was provably fine, which rules out a cable/port/hardware-negotiation
explanation and points at something in the *running application's* use of
that channel.

Investigated against real framework source
(`framework-arduinoespressif32/cores/esp32/HWCDC.cpp`, the ESP32-S3 native
USB Serial/JTAG peripheral driver Arduino's `Serial` sits on for this
board):

- `Serial.setTxTimeoutMs(0)` is already set in `main.cpp:217`. Confirmed
  this makes every internal `xSemaphoreTake`/`xRingbufferSend` call
  non-blocking (`timeout_ms / portTICK_PERIOD_MS` = 0 ticks) — ruled out
  "a blocking print stalls the calling task forever" as the cause; that
  failure mode is already defended against.
- `sleepModeOn()` (`src/powerSave.cpp`) — the CPU-frequency-changing,
  watchdog-disabling deep sleep mode — is never called anywhere in this
  codebase (grepped: zero call sites beyond its own definition). Ruled out
  as a cause; it's simply dead/unused code, unrelated to this incident.
- `HWCDC`'s own `write()` already has defensive handling for a host that
  stops reading (a bounded retry loop distinguishing "real disconnect" from
  "host backpressure," per its own inline comments) — this did not look like
  unpatched, naive framework code with an obvious blocking bug.

None of this converged on a root cause, and without a working serial link
there was no way to add temporary instrumentation and actually observe the
device's internal state during the failure — every hypothesis had to be
either speculative or discarded from source-reading alone.

## Decision

Add an HTTP endpoint (`GET /diag`, registered on the mirror's own
`AsyncWebServer` via `CardputerMirror.serverHandle()`) reporting USB serial
health as JSON, reachable over WiFi — a channel that's been reliable all
session, completely independent of USB:

```json
{"usbConnected":true,"usbPlugged":true,"consoleLoopTicks":123456,"freeHeap":181234,"uptimeMs":98765}
```

- `usbConnected` / `usbPlugged` — `Serial.isConnected()` /
  `Serial.isPlugged()`, HWCDC's own view of link state (the latter is
  backed by `usb_serial_jtag_is_connected()`, a timer-based check, not the
  older SOF-ISR one the framework's own comments note breaks esptool).
- `consoleLoopTicks` — a counter incremented once per
  `taskSerialConsole()` poll iteration (`src/serial_console.cpp`), exposed
  via `consoleLoopTicks()` (`src/serial_console.h`). A live, climbing
  number proves that task is scheduled and looping; a frozen number proves
  it isn't — the two have very different implications (task starved/dead
  vs. task alive but `Serial` itself not moving bytes).

Next time serial goes silent: hit `/diag` instead of guessing. If
`usbConnected` is false, it's a genuine link-level problem. If it's true
but `consoleLoopTicks` is frozen, the task itself is stuck — a completely
different, and more tractable, class of bug (deadlock/starvation, findable
by inspecting what else runs at the same or higher priority).

## Consequences

**Positive**

- Turns "USB went silent, no idea why" into an actual, host-reachable data
  point, without needing serial to already be working.
- Small, self-contained addition — one counter, one route, no change to
  any existing behavior or interface.

**Negative**

- Only helps if WiFi/HTTP is up when the incident happens. If the same
  underlying cause also takes down WiFi (unconfirmed either way), this
  endpoint is unreachable exactly when it's needed.
- Still doesn't explain the original incident. This is instrumentation for
  next time, not a resolution of this time.

**Neutral**

- Touches `src/main.cpp` and `src/serial_console.{h,cpp}` — outside
  `adapters/launcher/`, same category of necessary integration touch as the
  `interface.cpp`/`main.cpp` changes ADR 0003 and ADR 0008 already made.

## Alternatives considered

- **Log to SD/flash instead of exposing over HTTP.** Rejected for now:
  requires the failure to be reproduced with SD logging already enabled and
  then physically retrieving the card; the HTTP route needs no foresight
  beyond having WiFi up, and reads instantly from this Mac.
- **Add a periodic self-healing `Serial.end()`/`Serial.begin()` restart.**
  Considered, but rejected here: that treats a symptom without diagnosing
  it first, and could mask exactly the information `/diag` is meant to
  surface. Worth revisiting once `/diag` has actually caught the failure
  once and the real cause is known.
