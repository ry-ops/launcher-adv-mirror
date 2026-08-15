/*
 * LauncherAdapter -- launcher-adv-mirror ADR 0001.
 *
 * The only file in this project that knows both cardputer-adv-mirror and
 * Launcher exist. Implements cmirror::IHostAdapter (cardputer-adv-mirror ADR
 * 0038) against Launcher's real internals:
 *   - frameSource(): SpiReadbackFrameSource (cardputer-adv-mirror ADR 0039),
 *     since Launcher's Arduino_GFX build has no M5GFX to build the older
 *     ReadbackFrameSource against (see ADR 0002 in this repo).
 *   - inputSink(): enqueues remote key/button events; InputHandler() in
 *     boards/m5stack-cardputer/interface.cpp drains and applies them via
 *     applyMatrixKeyEvent() on its own task, in its own cycle (ADR 0003).
 *   - busLock(): nullptr -- Launcher's rendering has no dedicated task to
 *     lock against today (ADR 0004). Re-verify this on every upstream merge.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "CardputerMirror.h"
#include "SpiReadbackFrameSource.h"

class LauncherInputSink : public cmirror::IInputSink {
public:
    void begin() override;
    void inject(const cmirror::RemoteKey& k) override;
    void injectBtn(uint8_t btn, uint16_t ms) override;
};

class LauncherAdapter : public cmirror::IHostAdapter {
public:
    LauncherAdapter();

    void begin() override;
    cmirror::IFrameSource& frameSource() override { return _frameSource; }
    cmirror::IInputSink&   inputSink()   override { return _inputSink; }
    cmirror::PortMutex*    busLock()     override { return nullptr; }  // ADR 0004

    // Draws the same known pattern ReadbackFrameSource::selfTest() draws
    // (cardputer-adv-mirror ADR 0002), through Launcher's own tft API since
    // SpiReadbackFrameSource has no draw path of its own, then reads it back
    // and reports percent match. Call once after CardputerMirror.begin()
    // returns -- this is the one piece of "is readback trustworthy on THIS
    // build" signal Launcher gets; without it, a wrong dummy-bit count or
    // CS timing bug (cardputer-adv-mirror ADR 0039's stated risk) is
    // invisible until someone notices a garbled mirror image.
    int runSelfTest();

    // TEMPORARY passthrough for cardputer-adv-mirror's own TEMPORARY
    // diagnostic -- remove alongside SpiReadbackFrameSource::
    // debugSpiAddDeviceErr() once the real begin() failure is fixed.
    esp_err_t debugSpiErr() const { return _frameSource.debugSpiAddDeviceErr(); }

private:
    cmirror::SpiReadbackFrameSource _frameSource;
    LauncherInputSink               _inputSink;
};

extern LauncherAdapter launcherAdapter;
