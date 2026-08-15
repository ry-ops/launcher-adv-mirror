/*
 * LauncherCanvasFrameSource implementation.
 * SPDX-License-Identifier: MIT
 */
#include "LauncherCanvasFrameSource.h"

#if __has_include(<display.h>)
#include <display.h>
#endif

bool LauncherCanvasFrameSource::begin()
{
#ifdef USE_CANVAS
    if (!tft) return false;
    _fb = tft->getFramebuffer();
    _fbHeight = tft->height(); // logical (post-rotation) height, e.g. 135
    return _fb != nullptr && _fbHeight > 0;
#else
    return false; // USE_CANVAS not enabled in this build -- see this board's platformio.ini
#endif
}

bool LauncherCanvasFrameSource::fetchTile(int tileIndex, uint16_t* dst)
{
    if (!_fb) return false;
    using namespace cmirror;

    const int tx = (tileIndex % kTileCols) * kTileW;
    const int ty = (tileIndex / kTileCols) * kTileH;
    const int maxY = _fbHeight - 1;

    // Arduino_Canvas does NOT store its buffer row-major in the logical
    // (post-rotation) view. Confirmed directly from
    // Arduino_Canvas::writePixelPreclipped()'s rotation==1 case
    // (lib_modules/Arduino_GFX/src/canvas/Arduino_Canvas.cpp):
    //   fb += x * _height; fb += _max_y - y;
    // i.e. column-major: 240 columns of _height (135) elements each, Y
    // flipped within each column. This is the exact inverse of that write
    // formula -- getting this wrong doesn't crash, it just silently renders
    // a scrambled mirror image, so it's worth spelling out precisely here
    // rather than "obviously" doing row-major.
    for (int ly = 0; ly < kTileH; ++ly) {
        const int y = ty + ly;
        uint16_t* row = dst + (size_t)ly * kTileW;
        for (int lx = 0; lx < kTileW; ++lx) {
            const int x = tx + lx;
            row[lx] = _fb[(size_t)x * _fbHeight + (maxY - y)];
        }
    }
    return true;
}
