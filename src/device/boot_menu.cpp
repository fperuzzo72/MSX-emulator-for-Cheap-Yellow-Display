/* boot_menu.cpp - pick a machine and a ROM at power-on. See boot_menu.h. */
#include <Arduino.h>
#include <Preferences.h>

#include "panel.h"
#include "display.h"
#include "boot_menu.h"
#include "machine.h"

#define MENU_MAX      96
#define MENU_SECONDS  5      /* before it boots the remembered choice */

struct Entry { int machineIndex, entry; };
static Entry sEntries[MENU_MAX];
static int   sCount;

static const uint16_t COL_BG     = TFT_BLACK;
static const uint16_t COL_TITLE  = TFT_DARKGREY;
static const uint16_t COL_TEXT   = TFT_WHITE;
static const uint16_t COL_PICK   = TFT_YELLOW;
static const uint16_t COL_ROW    = 0x1082;   /* a very dark blue-grey */

#define ROW_TOP     48
#define ROW_HEIGHT  30
#define ROWS_SHOWN  7
#define SCROLL_X    432
#define SCROLL_W    (DISPLAY_PANEL_W - SCROLL_X)

/* The picture-scale button, below the list. */
#define BTN_X      16
#define BTN_W      200
#define BTN_H      34

/* The picture scale is remembered here rather than compiled in, so the
 * board comes back up the way it was left. It is applied even when the
 * menu is skipped for having nothing to choose. */
static int loadScale(void) {
    Preferences prefs;
    int v = 2;
    if (prefs.begin("cyd", true)) {
        v = prefs.getInt("scale", 2);
        prefs.end();
    }
    return (v == 1) ? 1 : 2;
}

static void saveScale(int v) {
    Preferences prefs;
    if (!prefs.begin("cyd", false)) return;
    prefs.putInt("scale", v);
    prefs.end();
}

static int sTop;   /* first row shown, for a list longer than the panel */

static int btnY(void) { return ROW_TOP + ROWS_SHOWN * ROW_HEIGHT + 8; }

static void drawScaleButton(TFT_eSPI &tft) {
    int y = btnY();
    int scale = display_get_scale();
    tft.fillRoundRect(BTN_X, y, BTN_W, BTN_H, 5, COL_ROW);
    tft.drawRoundRect(BTN_X, y, BTN_W, BTN_H, 5, COL_TITLE);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_TEXT, COL_ROW);
    tft.drawString(scale == 1 ? "picture  1:1  (small, crisp)"
                              : "picture  1.5x  (fills the screen)",
                   BTN_X + 10, y + 9, 2);
}

static void buildEntries(void) {
    sCount = 0;
    for (int m = 0; m < machine_count && sCount < MENU_MAX; m++) {
        int n = machine_list[m]->entry_count();
        for (int e = 0; e < n && sCount < MENU_MAX; e++) {
            sEntries[sCount].machineIndex = m;
            sEntries[sCount].entry = e;
            sCount++;
        }
    }
}

/* Reuse the touch calibration another firmware on this board may have
 * left in NVS. If there is none, TFT_eSPI's defaults are rough but the
 * rows here are 30 pixels tall and the whole width, so rough is enough. */
static void applyTouchCalibration(TFT_eSPI &tft) {
    Preferences prefs;
    uint16_t cal[5];
    if (!prefs.begin("cyd", true)) return;
    if (prefs.getBytesLength("touchcal") == sizeof(cal)) {
        prefs.getBytes("touchcal", cal, sizeof(cal));
        tft.setTouch(cal);
        Serial.println("menu: using the touch calibration stored on this board");
    }
    prefs.end();
}

/* One visible row, `slot` counting from the top of the window. */
static void drawRow(TFT_eSPI &tft, int slot, int i, bool current) {
    int y = ROW_TOP + slot * ROW_HEIGHT;
    tft.fillRect(0, y, SCROLL_X - 4, ROW_HEIGHT - 2, current ? COL_ROW : COL_BG);
    if (i < 0 || i >= sCount) return;

    const Machine *m = machine_list[sEntries[i].machineIndex];
    tft.setTextColor(current ? COL_PICK : COL_TEXT, current ? COL_ROW : COL_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(m->name, 10, y + 6, 2);
    tft.drawString(m->entry_name(sEntries[i].entry), 196, y + 6, 2);
}

static void drawScrollButtons(TFT_eSPI &tft) {
    int h = (ROWS_SHOWN * ROW_HEIGHT) / 2;
    tft.fillRect(SCROLL_X, ROW_TOP, SCROLL_W, h - 2, COL_ROW);
    tft.fillRect(SCROLL_X, ROW_TOP + h, SCROLL_W, h - 2, COL_ROW);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_TEXT, COL_ROW);
    tft.drawString("up", SCROLL_X + SCROLL_W / 2, ROW_TOP + h / 2, 2);
    tft.drawString("dn", SCROLL_X + SCROLL_W / 2, ROW_TOP + h + h / 2, 2);
    tft.setTextDatum(TL_DATUM);
}

static void drawMenu(TFT_eSPI &tft, int current, int secondsLeft) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_TITLE, COL_BG);
    tft.drawString("Choose a machine", 16, 16, 4);

    for (int slot = 0; slot < ROWS_SHOWN; slot++)
        drawRow(tft, slot, sTop + slot, sTop + slot == current);
    drawScrollButtons(tft);
    drawScaleButton(tft);

    tft.fillRect(0, DISPLAY_PANEL_H - 30, DISPLAY_PANEL_W, 30, COL_BG);
    tft.setTextColor(COL_TITLE, COL_BG);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d of %d  -  touch to choose  -  starting in %d",
             current + 1, sCount, secondsLeft);
    tft.drawString(buf, 16, DISPLAY_PANEL_H - 26, 2);
}

void boot_menu_run(void) {
    TFT_eSPI &tft = panel_tft();

    buildEntries();

    /* Whatever scale this board was left at, whether or not there is a
     * menu to show. */
    display_set_scale(loadScale());

    /* Nothing to choose between: don't make anyone look at a menu. */
    if (sCount <= 1) return;

    int current = 0;
    Serial.printf("menu: remembered machine %d, its entry %d, %d rows\n",
                  machine_chosen_index(),
                  machine_list[machine_chosen_index()]->selected_entry(), sCount);
    for (int i = 0; i < sCount; i++) {
        if (sEntries[i].machineIndex == machine_chosen_index() &&
            sEntries[i].entry == machine_list[sEntries[i].machineIndex]->selected_entry()) {
            current = i;
            break;
        }
    }

    /* An entry remembered from the serial console can be anywhere in a
     * long list, so scroll to it rather than starting at the top. */
    sTop = current - ROWS_SHOWN / 2;
    if (sTop > sCount - ROWS_SHOWN) sTop = sCount - ROWS_SHOWN;
    if (sTop < 0) sTop = 0;

    applyTouchCalibration(tft);
    tft.fillScreen(COL_BG);
    drawMenu(tft, current, MENU_SECONDS);

    uint32_t deadline = millis() + MENU_SECONDS * 1000;
    int shown = MENU_SECONDS;
    bool picked = false;

    while ((int32_t)(deadline - millis()) > 0) {
        uint16_t tx, ty;
        if (tft.getTouch(&tx, &ty)) {
            /* The scale button first: it changes a setting rather than
             * making a choice, so it must not also start the machine. */
            if ((int)ty >= btnY() && (int)ty < btnY() + BTN_H &&
                (int)tx >= BTN_X && (int)tx < BTN_X + BTN_W) {
                int next = display_get_scale() == 1 ? 2 : 1;
                display_set_scale(next);
                saveScale(next);
                drawScaleButton(tft);
                deadline = millis() + 60UL * 1000;   /* stop hurrying them */
                delay(200);                          /* one tap, one change */
                continue;
            }

            /* The scroll buttons. */
            if ((int)tx >= SCROLL_X && (int)ty >= ROW_TOP &&
                (int)ty < ROW_TOP + ROWS_SHOWN * ROW_HEIGHT) {
                int half = ROW_TOP + (ROWS_SHOWN * ROW_HEIGHT) / 2;
                sTop += ((int)ty < half) ? -ROWS_SHOWN : ROWS_SHOWN;
                if (sTop > sCount - ROWS_SHOWN) sTop = sCount - ROWS_SHOWN;
                if (sTop < 0) sTop = 0;
                drawMenu(tft, current, 0);
                deadline = millis() + 60UL * 1000;
                delay(200);
                continue;
            }

            int i = sTop + (((int)ty - ROW_TOP) / ROW_HEIGHT);
            if ((int)tx < SCROLL_X && (int)ty >= ROW_TOP && i >= 0 && i < sCount) {
                current = i;
                picked = true;
                drawMenu(tft, current, 0);
                /* A tap is a decision: start it, and let go of the wait. */
                delay(150);
                break;
            }
            /* A tap anywhere else just stops the countdown, so nobody has
             * to hurry while they read. */
            deadline = millis() + 60UL * 1000;
        }

        int left = (int)((deadline - millis()) / 1000) + 1;
        if (left != shown && left <= MENU_SECONDS) {
            shown = left;
            drawMenu(tft, current, left);
        }
        delay(20);
    }

    /* Only write a choice somebody made. Letting the countdown expire
     * used to store whatever row happened to be highlighted, which
     * quietly overwrote a selection made from the serial console when
     * that selection was further down the list than the menu could show. */
    if (picked) machine_choose(sEntries[current].machineIndex, sEntries[current].entry);
    Serial.printf("menu: starting %s / %s\n",
                  machine->name, machine->entry_name(machine->selected_entry()));

    tft.fillScreen(COL_BG);
}
