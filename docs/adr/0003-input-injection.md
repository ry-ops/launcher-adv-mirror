# ADR 0003 — Input injection: target Launcher's real `launcherInputLock`/`KeyStroke`

**Status:** Proposed
**Deciders:** firmware owner
**Related:** ADR 0001 (adapter architecture); cardputer-adv-mirror ADR 0038
(`IInputSink`, `RemoteKey`).

## Context

cardputer-adv-mirror's wire protocol already sends `(row, col, shift, fn)` —
the same matrix coordinate a physical Cardputer keypress produces — so a
remote press and a physical press converge on identical code paths
(`src/keyinject.h`'s own stated design). Whether that holds for Launcher
depends entirely on whether Launcher's key matrix agrees with M5Cardputer's.

**Verified, not assumed:** Launcher vendors its own `_key_value_map[4][14]`
in `lib/utility/Keyboard.h:41`. Diffed byte-for-byte against M5Cardputer's
own copy (`M5Cardputer/src/utility/Keyboard/Keyboard.h:18`, the table
cardputer-adv-mirror's wire protocol is built from) — **identical**, same
`[row][col]` axis convention. No translation table is needed.

What Launcher does with a `(row, col, pressed)` event today, for the ADV's
TCA8418 path, lives entirely inside `InputHandler()` in
`boards/m5stack-cardputer/interface.cpp`:

- `handleSpecialKeys(row, col, pressed)` — intercepts FN and Shift, and the
  Shift+FN caps-lock toggle.
- A long `if/else` chain matching `getKeyChar(row, col)` against Backspace,
  backtick (the ADV's Esc alias), Enter, the four `RESERVED_NAV_KEYS`
  (`;` `,` `.` `/` — Launcher's up/prev/down/next aliases), Tab, and the
  modifier keys — each building a `keyStroke` (`pendingKey`) with the right
  `.word`, `.enter`, `.fn`, `.del`, `.modifier_keys` fields set.
- Setting `NextPress` / `PrevPress` / `SelPress` / `EscPress` / `AnyKeyPress`
  from the same event, with repeat timers (`nextRepeatTime` etc.) for
  held-key auto-repeat while polling.
- The whole block runs under `launcherInputLock()` (`taskInputHandler`,
  `src/main.cpp`), guarding the same `KeyStroke` global `_getKeyPress()`
  reads from `loopTask`.

This logic is not currently factored into a reusable function — it's inline
inside the TCA8418 FIFO-draining loop, coupled to `kb_interrupt` and
`tca.getEvent()`.

## Decision

`LauncherAdapter::IInputSink::inject(const RemoteKey& k)`:

1. Calls `launcherInputLock()`.
2. Runs the **same** special-key / nav-key / modifier logic
   `InputHandler()`'s TCA8418 branch runs per event, applied to
   `(k.row, k.col, pressed)` — so a remote press is indistinguishable from
   a physical one by the time it reaches `KeyStroke`.
3. Sets `AnyKeyPress` (and `NextPress`/`PrevPress`/`SelPress`/`EscPress` as
   the matched key dictates), matching what the real event would have set.
4. Calls `launcherInputUnlock()`.

To avoid duplicating that ~80-line semantic block in two places (a real
maintenance hazard — Launcher's own key semantics changing would need to be
mirrored by hand in a duplicate), **extract it** from `InputHandler()` in
`boards/m5stack-cardputer/interface.cpp` into a small shared function —
e.g. `applyMatrixKeyEvent(uint8_t row, uint8_t col, bool pressed)` — called
by both the real TCA8418 branch and `LauncherAdapter::inject()`.

**This is a real, if small, edit to a Launcher file**, not a pure addition.
It's a deliberate, narrow exception to ADR 0001's "adapters/launcher/ is the
only new code" — flagged explicitly here rather than left implicit, and
worth re-checking for conflicts on every upstream merge (ADR 0005).

The **top-edge button** (BtnG0) is simpler and doesn't need extraction:
`InputHandler()` sets `SelPress = true` directly from
`launcherGpioRead(0) == LOW`, two lines. `injectBtn()` does the same under
the same lock.

**Auto-repeat is out of scope for v1.** `nextRepeatTime`/etc. exist to make
a *held* physical key repeat while `InputHandler()` polls every ~200ms; a
remote press has no "held" state unless the browser client resends it.
`inject()` fires the applied logic once per press/release pair it receives
and does not run its own repeat timer.

## Consequences

**Positive**

- No new key-mapping table to build, verify, or keep in sync — confirmed
  identical to what cardputer-adv-mirror already sends.
- Extracting the shared function means Launcher's own key-semantics changes
  (upstream) and this project's remote path can never silently diverge —
  a merge conflict on that function is exactly the signal that a recheck
  is needed.
- Remote presses are genuinely indistinguishable from physical ones once
  past `inject()` — same downstream code, same `KeyStroke` shape.

**Negative**

- Holding a key on the browser's on-screen keyboard won't auto-repeat
  server-side; the browser would need to resend presses itself to get that
  behavior. Worth a follow-up ADR if it turns out to matter in practice.
- One Launcher file (`interface.cpp`) now has a Cardputer-ADV-Mirror-shaped
  seam in it, which is one more thing to keep an eye on across upstream
  merges — see ADR 0001's Negative consequences and ADR 0005.

**Neutral**

- The extraction is small enough (~80 lines, one function boundary) that it
  should read as an obvious refactor if ever proposed upstream to
  `bmorcelli/Launcher` directly, which would remove even this exception.

## Alternatives considered

- **Duplicate the special-key logic inside the adapter instead of
  extracting it.** Rejected: two copies of ADV key semantics is exactly the
  kind of drift ADR 0001 exists to prevent, just moved into a different
  file.
- **Emulate raw TCA8418 I2C events and feed them through the existing
  `mapRawKeyToPhysical()` path unmodified.** Rejected: pointless
  round-trip — the mirror already has `(row, col)`, which is what
  `mapRawKeyToPhysical()` exists to *produce*. Feeding raw TCA8418 register
  values in would mean reconstructing them from `(row, col)` first.
- **Implement server-side auto-repeat for remote nav keys in v1.** Deferred,
  not rejected: adds a timer and repeat-rate matching decision with no
  evidence yet that it's needed for the launcher to feel usable remotely.
