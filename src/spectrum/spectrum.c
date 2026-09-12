/* spectrum.c - a ZX Spectrum 48K on the CYD.
 *
 * Shares the board with the MSX: the same panel layer, the same BLE
 * keyboard transport, the same serial console. What is here is only the
 * machine - memory map, ULA video, ports - plus the machine.h interface
 * the board talks through.
 *
 * The CPU is the Z80 already vendored for the MSX (lib/fmsx_core/Z80).
 * That core is a plain Z80 with no MSX in it; it wants RdZ80, WrZ80,
 * InZ80, OutZ80, PatchZ80 and LoopZ80 from whoever uses it, and this file
 * provides them.
 *
 * Deliberately not emulated yet: memory contention, the tape interface,
 * and the beeper's exact pulse timing. A 48K Spectrum without contention
 * runs everything except the handful of programs that count T-states.
 */
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "z80_names.h"
#include "Z80.h"
#include "machine.h"
#include "display.h"
#include "spectrum.h"

#ifdef HAVE_SPECTRUM_ROM
extern const unsigned char spectrum_rom[SPEC_ROM_SIZE];
#endif

/* ---------------------------------------------------------------- */
/* Machine state                                                      */
/* ---------------------------------------------------------------- */
static Z80      sCPU;
static uint8_t *sRAM;            /* 48kB, mapped at 0x4000 */
static uint8_t  sBorder = 7;
static uint8_t  sSpeaker;
static volatile int sReady;
static volatile unsigned long sFrames;
static int sSoundOn = 1;
static int64_t sNextFrameUs;

/* One band of the picture, the same trick the MSX side uses: a whole
 * 256x192 8bpp frame would be 48kB and this board has no PSRAM. */
#define BAND_LINES 24
static uint8_t *sBand;
static int sBandTop = -1, sBandFill;

/* Which bands have changed since they were last drawn.
 *
 * A Spectrum frame usually changes very little, and redrawing all 192
 * lines regardless was costing 78% of the frame - the blit, not the Z80,
 * was the whole bottleneck. Writes to the display file and to the
 * attributes mark the band they land in, and only marked bands are
 * redrawn. Everything that invalidates the whole picture - the flash
 * phase turning over, a border change, a scale change - marks all of
 * them. */
#define BANDS (192 / BAND_LINES)
static uint8_t sDirty[BANDS];
static uint8_t sLastBorder = 0xFF;

static void markAll(void) { memset(sDirty, 1, sizeof(sDirty)); }

/* The pixel row an address in the display file belongs to. The layout is
 * the interleaved one, so this is the inverse of screenAddr(). */
static void markAddress(uint16_t A) {
    int y;
    if (A >= SPEC_SCREEN && A < SPEC_ATTRS) {
        int o = A - SPEC_SCREEN;
        y = ((o & 0x1800) >> 5) | ((o & 0x0700) >> 8) | ((o & 0x00E0) >> 2);
        sDirty[y / BAND_LINES] = 1;
    } else if (A >= SPEC_ATTRS && A < SPEC_ATTRS + 768) {
        /* One attribute covers an 8-pixel cell, which can straddle two
         * bands only if BAND_LINES is not a multiple of 8. It is, but mark
         * both anyway rather than depend on that. */
        int row = (A - SPEC_ATTRS) / 32;
        sDirty[(row * 8) / BAND_LINES] = 1;
        sDirty[(row * 8 + 7) / BAND_LINES] = 1;
    }
}

/* The Spectrum's fifteen colours: eight at two brightnesses, with black
 * shared. RGB565, built the way the panel wants them. */
#define RGB565(r,g,b) (uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3))
static uint16_t sPalette[16];

static void buildPalette(void) {
    int i;
    for (i = 0; i < 16; i++) {
        int bright = i & 8;
        int lvl = bright ? 0xFF : 0xCD;   /* the usual normal/bright pair */
        int b = (i & 1) ? lvl : 0;
        int r = (i & 2) ? lvl : 0;
        int g = (i & 4) ? lvl : 0;
        sPalette[i] = RGB565(r, g, b);
    }
}

/* ---------------------------------------------------------------- */
/* Memory and ports                                                   */
/* ---------------------------------------------------------------- */
byte RdZ80(word A) {
#ifdef HAVE_SPECTRUM_ROM
    if (A < SPEC_ROM_SIZE) return spectrum_rom[A];
#else
    if (A < SPEC_ROM_SIZE) return 0xFF;
#endif
    return sRAM[A - SPEC_ROM_SIZE];
}

void WrZ80(word A, byte V) {
    if (A < SPEC_ROM_SIZE) return;      /* ROM is ROM */
    sRAM[A - SPEC_ROM_SIZE] = V;
    if (A < SPEC_ATTRS + 768) markAddress(A);
}

byte InZ80(word Port) {
    /* Anything with A0 low is the ULA. The keyboard half-rows are
     * selected by the high address byte, and unread bits float high. */
    if (!(Port & 0x0001)) return (byte)(spectrum_keys_read((uint8_t)(Port >> 8)) | 0xA0);
    return 0xFF;
}

void OutZ80(word Port, byte V) {
    if (!(Port & 0x0001)) {
        sBorder  = V & 0x07;
        sSpeaker = (V >> 4) & 1;
    }
}

void PatchZ80(Z80 *R) { (void)R; }

/* ---------------------------------------------------------------- */
/* Video                                                              */
/* ---------------------------------------------------------------- */

/* The display file is famously not linear: within a third of the screen,
 * consecutive addresses step eight pixel rows at a time. */
static uint16_t screenAddr(int y, int xByte) {
    /* Absolute, including the 0x4000 base, so callers can subtract
     * SPEC_ROM_SIZE to index sRAM. Returning a bare offset here and then
     * subtracting 0x4000 from it - which is what this did first - indexes
     * 16kB before the buffer. */
    return (uint16_t)(SPEC_SCREEN
                      | ((y & 0xC0) << 5) | ((y & 0x07) << 8)
                      | ((y & 0x38) << 2) | xByte);
}

static void flushBand(void) {
    if (sBandTop >= 0 && sBandFill > 0)
        display_write_picture(0, (short)(SPEC_PICTURE_TOP + sBandTop),
                              DISPLAY_PICTURE_W, (short)sBandFill,
                              sBand, sPalette[sBorder], sPalette);
    sBandTop = -1;
    sBandFill = 0;
}

static void fillRows(int top, int rows) {
    if (rows > 0)
        display_write_picture(0, (short)top, DISPLAY_PICTURE_W, (short)rows,
                              NULL, sPalette[sBorder], sPalette);
}

/* Render one pixel row of the picture into the band. */
static void renderLine(int y, int flashPhase) {
    const uint8_t *bitmap = sRAM + (screenAddr(y, 0) - SPEC_ROM_SIZE);
    const uint8_t *attrs  = sRAM + (SPEC_ATTRS - SPEC_ROM_SIZE) + (y >> 3) * 32;
    uint8_t *p;
    int x;

    if (sBandTop >= 0 && (sBandFill >= BAND_LINES || y != sBandTop + sBandFill))
        flushBand();
    if (sBandTop < 0) { sBandTop = y; sBandFill = 0; }

    p = sBand + sBandFill * DISPLAY_PICTURE_W;
    sBandFill++;

    for (x = 0; x < 32; x++) {
        uint8_t bits = bitmap[x];
        uint8_t a    = attrs[x];
        uint8_t ink  = (uint8_t)((a & 0x07) | ((a & 0x40) >> 3));
        uint8_t pap  = (uint8_t)(((a >> 3) & 0x07) | ((a & 0x40) >> 3));
        int i;
        /* Bit 7 of the attribute swaps ink and paper twice a second. */
        if ((a & 0x80) && flashPhase) { uint8_t t = ink; ink = pap; pap = t; }
        for (i = 0; i < 8; i++, bits <<= 1)
            *p++ = (bits & 0x80) ? ink : pap;
    }
}

/* ---------------------------------------------------------------- */
/* The frame                                                          */
/* ---------------------------------------------------------------- */
/* The CPU core calls this every IPeriod cycles. The frame is driven from
 * runFrame() instead, so there is nothing to do here but say "no
 * interrupt pending". */
word LoopZ80(Z80 *R) { (void)R; return INT_NONE; }

static void runFrame(void) {
    static int flashCounter, flashPhase;
    int y;

    display_service();
    if (display_take_repaint()) { sLastBorder = 0xFF; markAll(); }

    /* Border above the picture, then the picture, then the border below.
     * The border colour can change mid-frame on real hardware; this draws
     * it once per frame, which is right for everything that does not use
     * the border as an effect. */
    if (sBorder != sLastBorder) {
        fillRows(0, SPEC_PICTURE_TOP);
        fillRows(SPEC_PICTURE_TOP + 192, 216 - SPEC_PICTURE_TOP - 192);
        sLastBorder = sBorder;
    }

    for (y = 0; y < 192; y++)
        if (sDirty[y / BAND_LINES]) renderLine(y, flashPhase);
    flushBand();
    memset(sDirty, 0, sizeof(sDirty));

    /* The flash attribute swaps ink and paper twice a second, so every
     * cell using it has to be redrawn when the phase turns over. */
    if (++flashCounter >= 16) {
        flashCounter = 0;
        flashPhase = !flashPhase;
        markAll();
    }

    spectrum_keys_frame();

    ExecZ80(&sCPU, SPEC_FRAME_TSTATES);
    IntZ80(&sCPU, INT_IRQ);      /* IM1: 50Hz maskable interrupt */

    sFrames++;

    /* Pace the machine to 50Hz.
     *
     * With only the changed bands being redrawn there is time to spare -
     * it free-ran at 105 fps, which is a Spectrum running at twice speed,
     * and every game would be unplayable. Sleeping the remainder also
     * feeds the watchdog and leaves the other core alone. If a frame
     * overruns, the deadline is reset rather than carried forward, so a
     * slow patch does not turn into a sprint afterwards. */
    {
        int64_t now = esp_timer_get_time();
        if (!sNextFrameUs) sNextFrameUs = now;
        sNextFrameUs += 1000000 / 50;
        if (sNextFrameUs > now) {
            int ms = (int)((sNextFrameUs - now) / 1000);
            vTaskDelay(ms > 0 ? pdMS_TO_TICKS(ms) : 1);
        } else if (now - sNextFrameUs > 200000) {
            /* More than a fifth of a second behind: something held the
             * machine up and there is no point sprinting to catch up. */
            sNextFrameUs = now;
            vTaskDelay(1);
        } else {
            /* Slightly late, which happens every frame because a tick is
             * 1ms and a frame is 20. Keep the deadline rather than
             * resetting it, or the small overshoot compounds into running
             * at 46Hz instead of 50. */
            vTaskDelay(1);
        }
    }
}

/* ---------------------------------------------------------------- */
/* machine.h                                                          */
/* ---------------------------------------------------------------- */
static int m_prealloc(void) {
    if (!sBand)
        sBand = (uint8_t *)heap_caps_malloc(DISPLAY_PICTURE_W * BAND_LINES,
                                            MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    return sBand != 0;
}

static void m_run(void) {
#ifndef HAVE_SPECTRUM_ROM
    printf("spectrum: no ROM built in. See src/spectrum/spectrum.h - supply a\n"
           "          48K ROM and run tools/embed_rom.py. Stopping.\n");
    return;
#else
    buildPalette();
    spectrum_keys_reset();

    sRAM = (uint8_t *)heap_caps_malloc(SPEC_RAM_SIZE, MALLOC_CAP_8BIT);
    if (!sRAM) { printf("spectrum: could not allocate 48kB of RAM\n"); return; }
    memset(sRAM, 0, SPEC_RAM_SIZE);

    printf("spectrum: 48kB RAM at %p, ROM from flash, %d T-states a frame\n",
           (void *)sRAM, SPEC_FRAME_TSTATES);

    ResetZ80(&sCPU);
    sCPU.IPeriod = SPEC_FRAME_TSTATES;
    sCPU.IAutoReset = 1;

    markAll();
    sReady = 1;
    for (;;) runFrame();
#endif
}

static int m_ready(void) { return sReady; }
static unsigned long m_frames(void) { return sFrames; }

static void m_hid(const uint8_t report[8]) { spectrum_keys_hid(report); }
static int  m_type(const char *text)       { return spectrum_keys_type(text); }
static int  m_typing(void)                 { return spectrum_keys_typing(); }

static const char *m_screen_mode(void) { return "256x192, 32x24 attributes"; }

/* The Spectrum has no text mode: what is on screen is a bitmap. But the
 * ROM carries an 8x8 font, so a cell can be matched against it and named.
 * That is what makes the console's screen readback work here at all, and
 * it is how this machine gets checked over the USB cable the same way the
 * MSX is. */
static int m_screen_row(int row, uint8_t *out, int max) {
#ifndef HAVE_SPECTRUM_ROM
    (void)row; (void)out; (void)max;
    return 0;
#else
    int col, cols = 32 > max ? max : 32;
    if (!sRAM || row < 0 || row >= 24) return 0;

    for (col = 0; col < cols; col++) {
        uint8_t cell[8];
        int i, ch, found = ' ';
        for (i = 0; i < 8; i++)
            cell[i] = sRAM[screenAddr(row * 8 + i, col) - SPEC_ROM_SIZE];
        for (ch = 32; ch < 128; ch++) {
            const unsigned char *g = spectrum_rom + SPEC_FONT_ADDR + (ch - 32) * 8;
            if (!memcmp(cell, g, 8)) { found = ch; break; }
        }
        out[col] = (uint8_t)found;
    }
    return cols;
#endif
}

static int m_char_pattern(int code, uint8_t *rows8) {
#ifndef HAVE_SPECTRUM_ROM
    (void)code; (void)rows8;
    return 0;
#else
    int i;
    if (code < 32 || code > 127) return 0;
    for (i = 0; i < 8; i++)
        rows8[i] = spectrum_rom[SPEC_FONT_ADDR + (code - 32) * 8 + i];
    return 1;
#endif
}

static int m_peek(int addr) {
    if (addr < 0 || addr > 0xFFFF) return -1;
    if (addr < SPEC_ROM_SIZE) {
#ifdef HAVE_SPECTRUM_ROM
        return spectrum_rom[addr];
#else
        return -1;
#endif
    }
    return sRAM ? sRAM[addr - SPEC_ROM_SIZE] : -1;
}

static void m_set_sound(int on) { sSoundOn = on ? 1 : 0; }
static int  m_sound_on(void)    { return sSoundOn; }

static const char *m_debug_help(void) { return ""; }
static int m_debug_command(const char *line) { (void)line; return 0; }

/* Only one thing to boot into so far. Snapshots built into the firmware
 * would appear here, the same way the MSX's cartridges do. */
static int         m_entry_count(void)    { return 1; }
static const char *m_entry_name(int i)    { (void)i; return "Spectrum BASIC"; }
static void        m_select_entry(int i)  { (void)i; }
static int         m_selected_entry(void) { return 0; }

const Machine spectrum_machine = {
    "ZX Spectrum 48K",
    m_prealloc, m_run, m_ready, m_frames,
    m_hid, m_type, m_typing,
    m_screen_row, m_screen_mode, m_char_pattern, m_peek,
    m_set_sound, m_sound_on,
    m_entry_count, m_entry_name, m_select_entry, m_selected_entry,
    m_debug_command, m_debug_help,
};
