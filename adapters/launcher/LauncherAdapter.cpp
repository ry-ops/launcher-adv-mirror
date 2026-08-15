/*
 * LauncherAdapter -- launcher-adv-mirror ADR 0001 implementation.
 * SPDX-License-Identifier: MIT
 */
#include "LauncherAdapter.h"
#include "idf/launcher_platform.h"
#include "matrix_input.h"
#include <display.h>
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
    // Bounded drain, same reasoning as cardputer-adv-mirror's keyinject.cpp:
    // a full queue must not monopolize one InputHandler() cycle.
    for (int i = 0; i < 8 && xQueueReceive(s_remoteQ, &e, 0) == pdTRUE; ++i) {
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

namespace {

cmirror::SpiPanelConfig makeSpiPanelConfig()
{
    cmirror::SpiPanelConfig cfg;
    cfg.pinMosi = TFT_MOSI;
    cfg.pinSclk = TFT_SCLK;
    cfg.pinDc   = TFT_DC;
    cfg.pinCs   = TFT_CS;
    // Launcher's display bus is the default global `SPI` object
    // (display.cpp: new Arduino_HWSPI(..., &SPI)). arduino-esp32 constructs
    // that as SPIClass(FSPI) (SPI.cpp); FSPI == 1 on S3 (esp32-hal-spi.h),
    // and spi_num == FSPI maps to the SPI2 peripheral in spiStartBus()
    // (esp32-hal-spi.c: DPORT_SPI2_CLK_EN branch) -- i.e. SPI2_HOST, NOT
    // SPI3_HOST. cardputer-adv-mirror's own "SPI3_HOST" hardware fact is
    // M5GFX's choice of peripheral for the standalone build on the SAME
    // physical pins, not a property of the PCB traces -- the S3's SPI2/SPI3
    // controllers are both fully GPIO-matrixed. Getting this wrong fails
    // loudly (spi_bus_add_device errors, SpiReadbackFrameSource::begin()
    // logs and returns false) rather than silently reading garbage.
    cfg.host      = SPI2_HOST;
    cfg.colOffset = TFT_COL_OFS1;
    cfg.rowOffset = TFT_ROW_OFS1;
    return cfg;
}

}  // namespace

LauncherAdapter::LauncherAdapter() : _frameSource(makeSpiPanelConfig()) {}

void LauncherAdapter::begin()
{
    // Nothing else needed here: CardputerMirror ADR 0038's Mirror::begin()
    // already calls frameSource().begin() and inputSink().begin() itself as
    // part of the adapter-driven begin() sequence.
}

int LauncherAdapter::runSelfTest()
{
    // Same 32x16, 4-quadrant pattern and geometry as ReadbackFrameSource::
    // selfTest() in cardputer-adv-mirror (ADR 0002) -- drawn here through
    // Launcher's own tft instead of M5.Display, since that's the whole
    // reason SpiReadbackFrameSource exists (ADR 0039).
    const int W = 32, H = 16;
    static const uint16_t kPat[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};

    tft->startWrite();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            tft->drawPixel(x, y, kPat[((x >> 2) + (y >> 2)) & 3]);
    tft->endWrite();

    uint16_t expected[W * H];
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            expected[y * W + x] = kPat[((x >> 2) + (y >> 2)) & 3];

    const int score = _frameSource.checkPattern(0, 0, W, H, expected);
    launcherConsolePrintf(
        "CardputerMirror: SpiReadbackFrameSource self-test %d%%  <-- THE NUMBER THAT MATTERS\n",
        score
    );
    if (score < 0)       launcherConsolePrintf("  !! self-test could not allocate; inconclusive\n");
    else if (score >= 95) launcherConsolePrintf("  OK: readback looks reliable on this unit.\n");
    else if (score >= 60) launcherConsolePrintf("  MARGINAL: expect artifacts in the mirror.\n");
    else launcherConsolePrintf(
        "  FAIL: readback unusable on this unit -- check SPI2_HOST/pin config "
        "(cardputer-adv-mirror ADR 0039, this repo's ADR 0002).\n"
    );
    return score;
}

LauncherAdapter launcherAdapter;
