/* selector.cpp - swap cartridge or tape while the machine runs. See selector.h. */
#include <Arduino.h>

#include "panel.h"
#include "display.h"
#include "selector.h"
#include "machine.h"

#define ROW_TOP     44
#define ROW_HEIGHT  30
#define ROWS_SHOWN  8
#define SCROLL_X    432
#define SCROLL_W    (DISPLAY_PANEL_W - SCROLL_X)

static const uint16_t COL_BG   = TFT_BLACK;
static const uint16_t COL_ROW  = 0x1082;
static const uint16_t COL_TEXT = TFT_WHITE;
static const uint16_t COL_PICK = TFT_YELLOW;
static const uint16_t COL_DIM  = TFT_DARKGREY;

static bool sActive;
static int  sTop, sCurrent;
static bool sNeedsDraw;
static uint32_t sIgnoreTouchUntil;

void selector_open(void) {
    if (sActive) return;
    sActive = true;
    sNeedsDraw = true;
    sCurrent = machine->selected_entry();
    sTop = sCurrent - ROWS_SHOWN / 2;
    if (sTop > machine->entry_count() - ROWS_SHOWN) sTop = machine->entry_count() - ROWS_SHOWN;
    if (sTop < 0) sTop = 0;
    /* Whatever press opened this must not also pick something. */
    sIgnoreTouchUntil = millis() + 300;
}

int selector_active(void) { return sActive ? 1 : 0; }

/* A press held for about a second. A tap would open it by accident during
 * a game; a hold is deliberate and needs no particular place to aim at,
 * which matters because the picture moves around with the scale. */
#define HOLD_MS 900

void selector_poll_open(void) {
    static uint32_t downSince;
    uint16_t tx, ty;

    if (sActive) return;

    if (!panel_tft().getTouch(&tx, &ty)) { downSince = 0; return; }

    if (!downSince) { downSince = millis(); return; }
    if (millis() - downSince >= HOLD_MS) {
        downSince = 0;
        selector_open();
    }
}

static void draw(TFT_eSPI &tft) {
    int n = machine->entry_count();

    tft.fillScreen(COL_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString(machine->name, 10, 8, 2);
    tft.drawString("F12 or tap below the list to cancel", 10,
                   DISPLAY_PANEL_H - 22, 2);

    for (int slot = 0; slot < ROWS_SHOWN; slot++) {
        int i = sTop + slot;
        int y = ROW_TOP + slot * ROW_HEIGHT;
        bool cur = (i == sCurrent);
        tft.fillRect(0, y, SCROLL_X - 4, ROW_HEIGHT - 2, cur ? COL_ROW : COL_BG);
        if (i < 0 || i >= n) continue;
        tft.setTextColor(cur ? COL_PICK : COL_TEXT, cur ? COL_ROW : COL_BG);
        tft.drawString(machine->entry_name(i), 10, y + 6, 2);
    }

    int h = (ROWS_SHOWN * ROW_HEIGHT) / 2;
    tft.fillRect(SCROLL_X, ROW_TOP, SCROLL_W, h - 2, COL_ROW);
    tft.fillRect(SCROLL_X, ROW_TOP + h, SCROLL_W, h - 2, COL_ROW);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_TEXT, COL_ROW);
    tft.drawString("up", SCROLL_X + SCROLL_W / 2, ROW_TOP + h / 2, 2);
    tft.drawString("dn", SCROLL_X + SCROLL_W / 2, ROW_TOP + h + h / 2, 2);
    tft.setTextDatum(TL_DATUM);
}

int selector_frame(void) {
    if (!sActive) return -1;

    TFT_eSPI &tft = panel_tft();
    int n = machine->entry_count();

    if (sNeedsDraw) { draw(tft); sNeedsDraw = false; }

    uint16_t tx, ty;
    if (millis() < sIgnoreTouchUntil || !tft.getTouch(&tx, &ty)) return -1;

    if ((int)tx >= SCROLL_X && (int)ty >= ROW_TOP &&
        (int)ty < ROW_TOP + ROWS_SHOWN * ROW_HEIGHT) {
        int half = ROW_TOP + (ROWS_SHOWN * ROW_HEIGHT) / 2;
        sTop += ((int)ty < half) ? -ROWS_SHOWN : ROWS_SHOWN;
        if (sTop > n - ROWS_SHOWN) sTop = n - ROWS_SHOWN;
        if (sTop < 0) sTop = 0;
        sNeedsDraw = true;
        sIgnoreTouchUntil = millis() + 250;
        return -1;
    }

    int i = sTop + (((int)ty - ROW_TOP) / ROW_HEIGHT);
    if ((int)tx < SCROLL_X && (int)ty >= ROW_TOP && i >= 0 && i < n &&
        (int)ty < ROW_TOP + ROWS_SHOWN * ROW_HEIGHT) {
        sActive = false;
        return i;
    }

    /* Anywhere else closes it without changing anything. */
    sActive = false;
    return -1;
}
