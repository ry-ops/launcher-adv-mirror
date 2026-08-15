/*
 * matrix_input.h -- launcher-adv-mirror ADR 0003.
 *
 * Shares Launcher's real key semantics (handleSpecialKeys, nav-key aliases,
 * modifier handling, KeyStroke construction) between the real TCA8418 event
 * loop in InputHandler() and CardputerMirror's remote-key path, so a remote
 * press is genuinely indistinguishable from a physical one once past
 * applyMatrixKeyEvent() -- not a second, hand-copied implementation that can
 * drift from the first.
 *
 * WHY A SHARED STATE STRUCT, NOT TWO WRITERS OF NextPress/etc. DIRECTLY
 * ----------------------------------------------------------------------
 * InputHandler() runs on taskInputHandler and unconditionally overwrites
 * NextPress/PrevPress/UpPress/DownPress/SelPress/EscPress every cycle
 * (`NextPress = nextPulse;` etc.), computed fresh from this cycle's events.
 * If CardputerMirror's adapter wrote those globals directly from the
 * AsyncTCP task, InputHandler()'s own next cycle would clobber it before
 * loopTask ever read it -- two independent writers racing the same pulse-
 * style globals. Instead, the adapter only ever enqueues; MatrixInputState
 * is drained and applied from INSIDE InputHandler() itself, on the same
 * task, in the same cycle that also processes real TCA8418 events -- see
 * drainRemoteMatrixQueue().
 */
#pragma once
#include <globals.h>
#include <stdint.h>

// Per-event working state, promoted out of InputHandler()'s local statics so
// applyMatrixKeyEvent() can be called against it from more than one call
// site within the same InputHandler() cycle (real TCA8418 events, then
// queued remote events).
struct MatrixInputState {
    unsigned long nextRepeatTime = 0;
    unsigned long prevRepeatTime = 0;
    unsigned long upRepeatTime   = 0;
    unsigned long downRepeatTime = 0;

    // Held-key state, persists ACROSS InputHandler() cycles (a key stays
    // "down" until an event with pressed=false for the same key arrives).
    bool sel = false, prev = false, next = false, up = false, down = false, esc = false;

    // Per-cycle scratch -- reset at the top of every InputHandler() call,
    // accumulated across every event (real + remote) processed that cycle.
    bool keyEventHandled = false;
    bool nextPulse = false, prevPulse = false, upPulse = false, downPulse = false;
    bool delPulse = false, keyPulse = false;
    keyStroke pendingKey;
};

extern MatrixInputState g_matrixState;

// The exact per-event logic InputHandler()'s TCA8418 branch runs, extracted
// so it has exactly one implementation. Mutates st in place; does not touch
// KeyStroke/NextPress/etc. directly -- InputHandler() does that once, after
// all of this cycle's events (real and remote) have been folded into st.
void applyMatrixKeyEvent(MatrixInputState& st, uint8_t row, uint8_t col, bool pressed);

// Defined in adapters/launcher/LauncherAdapter.cpp, not here -- the queue
// itself is adapter state, not something boards/m5stack-cardputer needs to
// know the shape of. Pops everything currently queued and, for each event,
// calls applyMatrixKeyEvent(st, row, col, true) immediately followed by
// applyMatrixKeyEvent(st, row, col, false): CardputerMirror's wire protocol
// carries no separate press/release (see keyinject.h in cardputer-adv-mirror),
// so a remote key is synthesized as a momentary press-then-release rather
// than left "stuck down" with no release event to ever clear it.
void drainRemoteMatrixQueue(MatrixInputState& st);
