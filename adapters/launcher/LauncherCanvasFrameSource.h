/*
 * LauncherCanvasFrameSource -- launcher-adv-mirror.
 *
 * Reads pixels from Launcher's own Arduino_Canvas framebuffer (src/tft.h's
 * USE_CANVAS path) instead of the display SPI bus. Exists because
 * SpiReadbackFrameSource (cardputer-adv-mirror ADR 0039) can't attach to
 * that bus on this host at all: arduino-esp32 3.x's SPI class bypasses the
 * standard ESP-IDF bus-sharing bookkeeping it depends on (spi_bus_add_device
 * fails with ESP_ERR_INVALID_STATE, "host_id not initialized" -- confirmed
 * against the real IDF 5.5.4 source, not guessed). Reading a RAM
 * framebuffer sidesteps that whole class of problem: no SPI transaction, no
 * bus registration, nothing to conflict with Arduino's own display driving.
 *
 * Deliberately NOT in cardputer-adv-mirror's core: it references
 * Arduino_Canvas/Ard_eSPI types directly, and the core is meant to stay
 * driver-agnostic (ADR 0039's whole point) -- this is exactly the kind of
 * thing that belongs in the adapter instead.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "CardputerMirror.h"

class LauncherCanvasFrameSource : public cmirror::IFrameSource {
public:
    bool begin() override;
    bool fetchTile(int tileIndex, uint16_t* dst) override;

private:
    uint16_t* _fb       = nullptr;
    int       _fbHeight = 0; // Arduino_Canvas's logical (post-rotation) height
};
