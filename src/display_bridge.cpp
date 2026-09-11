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

/* Default-constructed on purpose. Passing (480,320) here - the landscape
 * size we actually draw in - tells TFT_eSPI the panel is NATIVELY 480x320,
 * and it is not: it is a 320x480 portrait panel that setRotation(1) turns
 * into a landscape one. With the wrong native size every setAddrWindow
 * lands somewhere else, which is what put garbage on the screen. The
 * working driver for this same board (CYD-MicroBASIC-MicroWriter,
 * editor/src/main.cpp) default-constructs it too. */
static TFT_eSPI tft;
static uint16_t lineBuf[WIDTH_OVERLAY]; /* sized for the largest possible width param (full-screen clear) */

/* TFT_eSPI sends a uint16_t buffer to the panel in the CPU's byte order
 * unless told otherwise, and the panel wants RGB565 the other way round.
 * Getting this wrong does not blank the screen - it scrambles every
 * colour, which is exactly what a garbled picture looks like. It is a
 * runtime setting rather than a constant here so the `w` command in the
 * serial console can flip it and settle the question on the panel. */
static bool sSwapBytes = true;

extern "C" void display_set_swap_bytes(int on) {
    sSwapBytes = on ? true : false;
    tft.setSwapBytes(sSwapBytes);
}

extern "C" int display_get_swap_bytes(void) { return sSwapBytes ? 1 : 0; }

void display_bridge_init() {
    /* Drive the backlight before anything else, the way the working
     * driver for this board does. */
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

    tft.init();
    /* Panel is portrait-native (320x480); rotate to landscape so the
     * 256x212 MSX picture centers nicely on the wider dimension. If your
     * picture comes up mirrored/upside-down, try rotation 3 instead. */
    tft.setRotation(1);
    tft.setSwapBytes(sSwapBytes);
    tft.fillScreen(TFT_BLACK);
}

/* Draw something known, straight to the panel, with the emulator out of
 * the picture. This separates the two things that both look like "garbage
 * on screen": wrong geometry (the picture is in the wrong place, torn, or
 * the wrong size) and wrong colour order (the picture is in the right
 * place but every colour is nonsense). */
extern "C" void display_test_pattern(int which) {
    switch (which) {
        case 0: tft.fillScreen(TFT_BLACK); break;
        case 1: tft.fillScreen(TFT_RED);   break;
        case 2: tft.fillScreen(TFT_GREEN); break;
        case 3: tft.fillScreen(TFT_BLUE);  break;
        case 4:
            /* A white block exactly where the MSX picture is drawn, with
             * a one-pixel red frame around the whole panel. */
            tft.fillScreen(TFT_BLACK);
            tft.drawRect(0, 0, WIDTH_OVERLAY, HEIGHT_OVERLAY, TFT_RED);
            tft.fillRect(MSX_DISPLAY_X, MSX_DISPLAY_Y, WIDTH, 212, TFT_WHITE);
            break;
        case 5: {
            /* The same block, but drawn the way the emulator draws it:
             * through display_write_frame_msx, in 24-line bands, out of a
             * palette. If case 4 looks right and this does not, the fault
             * is in the band path rather than in the panel setup. */
            /* Borrow the emulator's own band buffer rather than keeping
             * a second one: 6kB of .bss here is 6kB the BLE stack does
             * not get. The next emulated frame overwrites it anyway. */
            extern uint8_t *msxFramebuffer;
            uint8_t *band = msxFramebuffer;
            static uint16_t pal[16];
            if (!band) break;
            for (int i = 0; i < 16; i++) pal[i] = tft.color565((uint8_t)(i * 17), (uint8_t)(255 - i * 17), 0x40);
            tft.fillScreen(TFT_BLACK);
            for (int top = 0; top + 24 <= 216; top += 24) {
                for (int y = 0; y < 24; y++)
                    for (int x = 0; x < WIDTH; x++)
                        band[y * WIDTH + x] = (uint8_t)(((top + y) / 8 + x / 16) & 0x0F);
                display_write_frame_msx(MSX_DISPLAY_X, MSX_DISPLAY_Y + top, WIDTH, 24,
                                        band, pal[0], pal);
            }
            break;
        }
        default: break;
    }
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
