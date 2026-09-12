/* boot_menu.cpp - pick a machine and a ROM at power-on. See boot_menu.h. */
#include <Arduino.h>
#include <Preferences.h>

#include "panel.h"
#include "display.h"
#include "boot_menu.h"
#include "machine.h"

#define MENU_MAX      16
#define MENU_SECONDS  5      /* before it boots the remembered choice */

struct Entry { int machineIndex, entry; };
static Entry sEntries[MENU_MAX];
static int   sCount;

static const uint16_t COL_BG     = TFT_BLACK;
static const uint16_t COL_TITLE  = TFT_DARKGREY;
static const uint16_t COL_TEXT   = TFT_WHITE;
static const uint16_t COL_PICK   = TFT_YELLOW;
static const uint16_t COL_ROW    = 0x1082;   /* a very dark blue-grey */

#define ROW_TOP    56
#define ROW_HEIGHT 30

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

static void drawRow(TFT_eSPI &tft, int i, bool current) {
    const Machine *m = machine_list[sEntries[i].machineIndex];
    int y = ROW_TOP + i * ROW_HEIGHT;

    tft.fillRect(0, y, DISPLAY_PANEL_W, ROW_HEIGHT - 2, current ? COL_ROW : COL_BG);
    tft.setTextColor(current ? COL_PICK : COL_TEXT, current ? COL_ROW : COL_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(m->name, 16, y + 6, 2);
    tft.drawString(m->entry_name(sEntries[i].entry), 210, y + 6, 2);
}

static void drawMenu(TFT_eSPI &tft, int current, int secondsLeft) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_TITLE, COL_BG);
    tft.drawString("Choose a machine", 16, 16, 4);

    for (int i = 0; i < sCount; i++) drawRow(tft, i, i == current);

    tft.fillRect(0, DISPLAY_PANEL_H - 30, DISPLAY_PANEL_W, 30, COL_BG);
    tft.setTextColor(COL_TITLE, COL_BG);
    char buf[64];
    snprintf(buf, sizeof(buf), "touch to choose  -  starting in %d", secondsLeft);
    tft.drawString(buf, 16, DISPLAY_PANEL_H - 26, 2);
}

void boot_menu_run(void) {
    TFT_eSPI &tft = panel_tft();

    buildEntries();

    /* Nothing to choose between: don't make anyone look at a menu. */
    if (sCount <= 1) return;

    int current = 0;
    for (int i = 0; i < sCount; i++) {
        if (sEntries[i].machineIndex == machine_chosen_index() &&
            sEntries[i].entry == machine_list[sEntries[i].machineIndex]->selected_entry()) {
            current = i;
            break;
        }
    }

    applyTouchCalibration(tft);
    tft.fillScreen(COL_BG);
    drawMenu(tft, current, MENU_SECONDS);

    uint32_t deadline = millis() + MENU_SECONDS * 1000;
    int shown = MENU_SECONDS;

    while ((int32_t)(deadline - millis()) > 0) {
        uint16_t tx, ty;
        if (tft.getTouch(&tx, &ty)) {
            int i = ((int)ty - ROW_TOP) / ROW_HEIGHT;
            if (i >= 0 && i < sCount) {
                current = i;
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

    machine_choose(sEntries[current].machineIndex, sEntries[current].entry);
    Serial.printf("menu: starting %s / %s\n",
                  machine->name, machine->entry_name(sEntries[current].entry));

    tft.fillScreen(COL_BG);
}
