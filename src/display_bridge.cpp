/* display_bridge.cpp
 *
 * Implements display_write_frame_msx() (declared in msx_display.h) using
 * TFT_eSPI, wired to the FNK0103 3.5" ST7796 panel's real pinout (pulled
 * from Freenove's own TFT_eSPI setup file for this board - see
 * platformio.ini build_flags for the pin numbers and README for source).
 */
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "msx_display.h"

static TFT_eSPI tft = TFT_eSPI(WIDTH_OVERLAY, HEIGHT_OVERLAY);
static uint16_t lineBuf[WIDTH_OVERLAY]; /* sized for the largest possible width param (full-screen clear) */

void display_bridge_init() {
    tft.init();
    /* Panel is portrait-native (320x480); rotate to landscape so the
     * 256x212 MSX picture centers nicely on the wider dimension. If your
     * picture comes up mirrored/upside-down, try rotation 3 instead. */
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
}

extern "C" void display_write_frame_msx(short left, short top, short width, short height,
                                         const uint8_t *buffer, uint16_t bgColor,
                                         const uint16_t *palette) {
    if (width <= 0 || height <= 0) return;
    if (width > WIDTH_OVERLAY) width = WIDTH_OVERLAY; /* clamp defensively; should never trigger */

    tft.startWrite();
    tft.setAddrWindow(left, top, width, height);

    if (!buffer) {
        /* Flat fill (screen clear / border). */
        for (int i = 0; i < width; i++) lineBuf[i] = bgColor;
        for (int y = 0; y < height; y++) {
            tft.pushPixels(lineBuf, width);
        }
    } else {
        for (int y = 0; y < height; y++) {
            const uint8_t *row = buffer + (size_t)y * width;
            for (int x = 0; x < width; x++) lineBuf[x] = palette[row[x]];
            tft.pushPixels(lineBuf, width);
        }
    }

    tft.endWrite();
}
