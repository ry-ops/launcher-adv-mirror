/*
 * LauncherAdapter -- launcher-adv-mirror ADR 0001 implementation.
 * SPDX-License-Identifier: MIT
 */
#include "LauncherAdapter.h"
#include "matrix_input.h"
#include "powerSave.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace {

// One queue for both kinds, same reasoning as cardputer-adv-mirror's own
// keyinject.cpp: a single queue preserves ordering between a key and a
// button press, which two separate queues would not.
enum class Kind : uint8_t { Key, Btn };
struct RemoteEvt {
    Kind    kind;
    uint8_t row, col;
};

QueueHandle_t s_remoteQ = nullptr;
uint32_t      s_dropped = 0;

}  // namespace

void LauncherInputSink::begin()
{
    // 16 events is ~2 s of fast human typing (same sizing cardputer-adv-mirror's
    // keyinject.cpp uses) -- deliberately small so a flooding browser is
    // dropped at the door instead of building a backlog that replays keys
    // seconds after the user stopped pressing.
    if (!s_remoteQ) s_remoteQ = xQueueCreate(16, sizeof(RemoteEvt));
}

void LauncherInputSink::inject(const cmirror::RemoteKey& k)
{
    // Called from the AsyncTCP task -- enqueue only. shift/fn are already
    // encoded in which (row, col) the browser sent (same matrix coordinate a
    // physical press would produce), so nothing else needs to travel with
    // the event -- see cardputer-adv-mirror's keyinject.h for why.
    if (!s_remoteQ) return;
    RemoteEvt e{Kind::Key, k.row, k.col};
    if (xQueueSend(s_remoteQ, &e, 0) != pdTRUE) ++s_dropped;
}

void LauncherInputSink::injectBtn(uint8_t btn, uint16_t ms)
{
    // Only BtnG0 exists to inject (matches cardputer-adv-mirror's own
    // keyinject.cpp -- BtnRst cuts power to the SoC, no software press exists
    // to simulate). `ms` is accepted for interface parity with cardputer-adv-
    // mirror's onBtn() but unused: Launcher reads G0 as an instantaneous GPIO
    // sample (launcherGpioRead(0) == LOW) in InputHandler(), it has no held-
    // duration concept the way M5Unified's BtnA.setRawState() does.
    if (!s_remoteQ || btn != 0) return;
    RemoteEvt e{Kind::Btn, 0, 0};
    if (xQueueSend(s_remoteQ, &e, 0) != pdTRUE) ++s_dropped;
}

// Declared in matrix_input.h, called from InputHandler() in
// boards/m5stack-cardputer/interface.cpp -- on taskInputHandler, already
// under launcherInputLock() (see main.cpp's taskInputHandler). Deliberately
// does NOT take that lock itself: doing so from LauncherInputSink::inject()
// on the AsyncTCP task could block it for however long InputHandler() is
// mid-cycle, which is exactly the kind of blocking the IInputSink contract
// rules out. Enqueue-then-drain avoids that entirely.
void drainRemoteMatrixQueue(MatrixInputState& st)
{
    if (!s_remoteQ) return;
    RemoteEvt e;
    // launcher-adv-mirror ADR 0008: remote presses must feed the idle timer
    // the exact same way physical TCA8418 presses do (see this function's
    // caller in interface.cpp: `if (!wokeScreen) wokeScreen = wakeUpScreen();
    // if (wokeScreen) continue;`). Without this, wakeUpScreen() is never
    // called on this path at all -- the device dims and sleeps out from
    // under an active remote session, and the dark screen looks exactly
    // like "remote keys aren't working" even though they're still landing.
    // Mirrors physical precedent exactly: wake on the first event, then
    // discard this whole drained batch rather than also applying it as a
    // navigation press -- a physical press that wakes the screen doesn't
    // separately act as a menu press either.
    bool wokeScreen = false;
    // Bounded drain, same reasoning as cardputer-adv-mirror's keyinject.cpp:
    // a full queue must not monopolize one InputHandler() cycle.
    for (int i = 0; i < 8 && xQueueReceive(s_remoteQ, &e, 0) == pdTRUE; ++i) {
        if (!wokeScreen) wokeScreen = wakeUpScreen();
        if (wokeScreen) continue;
        if (e.kind == Kind::Btn) {
            // No software "virtual press" seam for G0 the way M5Unified's
            // BtnA.setRawState() gives cardputer-adv-mirror's own
            // keyinject.cpp -- set the same globals a real G0 press sets
            // two lines earlier in InputHandler(), in this same cycle.
            SelPress   = true;
            AnyKeyPress = true;
            continue;
        }
        // CardputerMirror's wire protocol carries no separate press/release
        // (see RemoteKey in IInputSink.h) -- synthesize a momentary
        // press-then-release rather than leaving held-key state (st.esc,
        // st.sel, ...) stuck true with no release event to ever clear it.
        applyMatrixKeyEvent(st, e.row, e.col, /*pressed=*/true);
        applyMatrixKeyEvent(st, e.row, e.col, /*pressed=*/false);
    }
}

uint32_t remoteInputDropped() { return s_dropped; }

void LauncherAdapter::begin()
{
    // Nothing else needed here: CardputerMirror ADR 0038's Mirror::begin()
    // already calls frameSource().begin() and inputSink().begin() itself as
    // part of the adapter-driven begin() sequence.
}

LauncherAdapter launcherAdapter;
