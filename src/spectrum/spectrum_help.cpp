/* spectrum_help.cpp - the keyword crib, drawn in the panel's margins.
 *
 * A 48K Spectrum puts a whole BASIC keyword on each key, and which one is
 * printed on the keycap of a real Spectrum and on nothing else. Coming to
 * it from a PC keyboard, the first thing anyone needs is that map.
 *
 * There is room for it without covering the picture: at 1:1 the 256-pixel
 * picture sits in the middle of a 480-pixel panel and leaves 112 pixels
 * of margin either side. At 1.5x it leaves 48, which is not enough for
 * anything readable, so turning the crib on also drops the scale to 1:1
 * and turning it off puts the scale back.
 *
 * F1 toggles it.
 */
#include <Arduino.h>
#include "panel.h"
#include "display.h"
#include "spectrum.h"

/* K mode, which is where a line starts: one keyword per letter. */
static const char *const kKeywords[26] = {
    "NEW",    /* A */ "BORDER", /* B */ "CONT",   /* C */ "DIM",    /* D */
    "REM",    /* E */ "FOR",    /* F */ "GOTO",   /* G */ "GOSUB",  /* H */
    "INPUT",  /* I */ "LOAD",   /* J */ "LIST",   /* K */ "LET",    /* L */
    "PAUSE",  /* M */ "NEXT",   /* N */ "POKE",   /* O */ "PRINT",  /* P */
    "PLOT",   /* Q */ "RUN",    /* R */ "SAVE",   /* S */ "RAND",   /* T */
    "IF",     /* U */ "CLS",    /* V */ "DRAW",   /* W */ "CLEAR",  /* X */
    "RETURN", /* Y */ "COPY",   /* Z */
};

static bool sActive;
static bool sNeedsDraw;
static int  sScaleBefore = 2;

int  spectrum_help_active(void) { return sActive ? 1 : 0; }
void spectrum_help_invalidate(void) { sNeedsDraw = sActive; }

void spectrum_help_toggle(void) {
    sActive = !sActive;
    if (sActive) {
        sScaleBefore = display_get_scale();
        display_set_scale(1);       /* the margins only exist at 1:1 */
    } else {
        display_set_scale(sScaleBefore);
    }
    sNeedsDraw = true;
}

/* Called once a frame from the machine's own task: TFT_eSPI may only be
 * driven from one of them. */
void spectrum_help_draw(void) {
    if (!sActive || !sNeedsDraw) return;
    sNeedsDraw = false;

    TFT_eSPI &tft = panel_tft();
    const int picLeft  = (DISPLAY_PANEL_W - DISPLAY_PICTURE_W) / 2;   /* 112 */
    const int picRight = picLeft + DISPLAY_PICTURE_W;                 /* 368 */

    tft.fillRect(0, 0, picLeft, DISPLAY_PANEL_H, TFT_BLACK);
    tft.fillRect(picRight, 0, DISPLAY_PANEL_W - picRight, DISPLAY_PANEL_H, TFT_BLACK);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("KEYWORDS", 6, 4, 2);
    tft.drawString("F1 hides", picRight + 6, 4, 2);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    for (int i = 0; i < 26; i++) {
        /* A to M down the left, N to Z down the right. */
        int left = i < 13;
        int x = left ? 6 : picRight + 6;
        int y = 26 + (left ? i : i - 13) * 16;
        char buf[16];
        snprintf(buf, sizeof(buf), "%c %s", 'A' + i, kKeywords[i]);
        tft.drawString(buf, x, y, 2);
    }

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("SYM+key", 6, 26 + 13 * 16 + 6, 2);
    tft.drawString("for punct.", 6, 26 + 13 * 16 + 22, 2);
    tft.drawString("CAPS+0 del", picRight + 6, 26 + 13 * 16 + 6, 2);
    tft.drawString("Esc BREAK", picRight + 6, 26 + 13 * 16 + 22, 2);
}
