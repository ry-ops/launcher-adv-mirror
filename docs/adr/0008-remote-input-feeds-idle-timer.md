# ADR 0008 — Remote key presses must feed the idle/sleep timer

**Status:** Accepted — implemented. Root cause confirmed against real
source; fix verified logically to mirror the physical path exactly. Live
hardware re-verification (does the screen actually stay awake during a
remote session now) still pending.
**Deciders:** firmware owner
**Related:** ADR 0003 (input injection targets `applyMatrixKeyEvent()`
directly), which is what let this go unnoticed — the wire protocol and
matrix injection were both correct, this bug is entirely about a step
neither of them was responsible for.

## Context

Reported live during hardware testing: "the web keyboard doesn't seem to be
working. but when i switch to my mac keyboard it works" — followed shortly
by "display and adv goes to sleep when using my mac keyboard."

Traced to `boards/m5stack-cardputer/interface.cpp`'s `InputHandler()`.
Launcher's idle/sleep mechanism (`src/powerSave.cpp`) dims then blanks the
display after `dimmerSet` seconds since `previousMillis` was last touched;
`previousMillis` is only ever updated inside `wakeUpScreen()`. The physical
TCA8418 path calls it correctly, once per drained FIFO burst, and discards
that whole burst if the screen was actually asleep (so the press that wakes
the screen doesn't also register as a menu navigation):

```cpp
if (!wokeScreen) wokeScreen = wakeUpScreen();
if (wokeScreen) continue;
applyMatrixKeyEvent(st, row, col, pressed);
```

`drainRemoteMatrixQueue(st)` — the function both the on-screen keyboard and
the "Capture my keyboard" browser feature ultimately drive (ADR 0003) — is
called one line above that block, entirely outside it, and never calls
`wakeUpScreen()` at all. Confirmed by grep: no call site of
`wakeUpScreen()` exists anywhere in `adapters/launcher/`.

Consequence: a remote session that never touches the physical device keeps
sending correctly-applied key events (the wire protocol and matrix
injection were never broken), while Launcher's idle timer keeps counting
down in the background, oblivious to any of it. The display dims, then
blanks, mid-session — which looks exactly like "remote keys stopped
working," because the one piece of feedback the user has (the physical
screen) goes dark for a reason that has nothing to do with whether the
presses are landing.

## Decision

`drainRemoteMatrixQueue()` now mirrors the physical FIFO-drain path exactly:
call `wakeUpScreen()` once, only when there's at least one queued remote
event to process (matching "don't touch the idle timer on an empty poll"),
and if it returns true (the screen was actually asleep/dimmed), discard the
rest of that drained batch rather than also applying it as a navigation
press — same reasoning as the physical path: the press that wakes the
screen isn't also a menu press.

## Consequences

**Positive**

- A remote session now keeps the screen awake for as long as it's actually
  sending input, matching what a user watching the physical device expects.
- Zero risk of silently swallowing input beyond the one wake-triggering
  batch — after that, remote presses apply exactly as before.
- No change to the wire protocol, `applyMatrixKeyEvent()`, or anything in
  cardputer-adv-mirror — this is entirely a Launcher-adapter-side gap.

**Negative**

- None identified. This restores parity with the physical input path; it
  doesn't add new behavior beyond what physical presses already do.

**Neutral**

- Doesn't address whatever originally made the on-screen keyboard "seem"
  broken in this same session — that may simply have BEEN this bug (screen
  asleep, keys landing fine underneath) rather than a separate defect. Worth
  re-testing live before assuming there's a second bug here.

## Alternatives considered

- **Call `wakeUpScreen()` inside `applyMatrixKeyEvent()` itself**, so both
  physical and remote paths share one call site. Rejected: the physical path
  needs the "discard this whole burst" behavior at the FIFO-drain level, not
  per individual key event, and `applyMatrixKeyEvent()` has no concept of
  "burst" — replicating the exact same batch-level pattern at the remote
  drain call site keeps both paths' semantics identical without reshaping
  the physical path's existing, working logic.
