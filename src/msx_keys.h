/* msx_keys.h
 *
 * Maps standard USB HID keyboard usage codes (as sent by any BLE keyboard
 * using the HID-over-GATT Boot Keyboard Input Report) to fMSX's KBD_SET/
 * KBD_RES key-matrix constants (see lib/fmsx_core/fMSX/MSX.h).
 *
 * IMPORTANT: fMSX's Keys[] table stores ONE matrix position per physical
 * key (e.g. '1' and '!' are the same physical key on a real MSX). Shift
 * is a separate matrix contact. So this table only needs the *unshifted*
 * base symbol for each key - do not try to encode shifted symbols here,
 * the MSX BIOS combines the Shift key state with the base key itself,
 * exactly like the real hardware.
 *
 * This table follows the standard US-QWERTY *physical position* -> HID
 * usage code assignment (that part is a USB standard, true regardless of
 * what's printed on your keycaps or what OS layout you have selected -
 * a HID keyboard sends raw physical-key codes, not characters).
 *
 * WHAT STILL NEEDS TUNING FOR A BRAZILIAN (ABNT2) LAYOUT / BIOS:
 *   - Row/col positions for accented letters (ç, á, é, í, ó, ú, ã, õ, â,
 *     ê, ô) and the ABNT2-specific keys (the extra key next to right-
 *     Shift, the dead-key behavior of ´ and ~) are NOT standard ASCII and
 *     are not in fMSX's stock Keys[] table. Which matrix row/col they sit
 *     on depends on which Brazilian MSX model your BIOS dump is from
 *     (Gradiente Expert, Sharp/Epcom Hotbit, Sony HB, etc). Once you tell
 *     me the model, I can extend fMSX's Keys[] table (in
 *     lib/fmsx_core/fMSX/MSX.c) with the correct matrix bits and wire
 *     the extra HID keycodes (0x32 non-US #, 0x64 non-US \, the ABNT
 *     usage codes 0x87/0x88) to them here.
 *   - Until then, those keys are simply ignored (harmless no-op), and
 *     everything else (letters, digits, punctuation available on a US
 *     layout, function/control keys, arrows) works normally.
 */
#ifndef MSX_KEYS_H
#define MSX_KEYS_H

#include <stdint.h>

extern "C" {
#include "MSX.h" /* KBD_* constants, KBD_SET/KBD_RES macros, KeyState[] */
}

/* HID modifier byte bit -> KBD_* constant. 0 means "not mapped". */
struct HidModifierMap {
    uint8_t bit;
    uint8_t kbd;
};

static const HidModifierMap kHidModifiers[] = {
    {0x01, KBD_CONTROL},  /* Left Ctrl  */
    {0x02, KBD_SHIFT},    /* Left Shift */
    {0x04, KBD_GRAPH},    /* Left Alt -> MSX GRAPH */
    {0x08, 0},            /* Left GUI/Cmd/Win - unmapped */
    {0x10, KBD_CONTROL},  /* Right Ctrl */
    {0x20, KBD_SHIFT},    /* Right Shift */
    {0x40, KBD_COUNTRY},  /* Right Alt -> MSX COUNTRY/kana key (adjust if you'd rather have GRAPH) */
    {0x80, 0},            /* Right GUI/Cmd/Win - unmapped */
};

/* HID keycode (0x00-0x67) -> fMSX key-matrix token (ASCII char or KBD_*
 * constant). 0 = no mapping (ignored). Index = HID usage code. */
static const uint8_t kHidKeycodeToMsx[104] = {
    /* 0x00 */ 0, 0, 0, 0,
    /* 0x04 */ 'a','b','c','d','e','f','g','h','i','j','k','l','m',
    /* 0x11 */ 'n','o','p','q','r','s','t','u','v','w','x','y','z',
    /* 0x1E */ '1','2','3','4','5','6','7','8','9','0',
    /* 0x28 */ KBD_ENTER,
    /* 0x29 */ KBD_ESCAPE,
    /* 0x2A */ KBD_BS,
    /* 0x2B */ KBD_TAB,
    /* 0x2C */ KBD_SPACE,
    /* 0x2D */ '-',
    /* 0x2E */ '=',
    /* 0x2F */ '[',
    /* 0x30 */ ']',
    /* 0x31 */ '\\',
    /* 0x32 */ 0,   /* Non-US #/~ : layout dependent, see header comment */
    /* 0x33 */ ';',
    /* 0x34 */ '\'',
    /* 0x35 */ '`',
    /* 0x36 */ ',',
    /* 0x37 */ '.',
    /* 0x38 */ '/',
    /* 0x39 */ KBD_CAPSLOCK,
    /* 0x3A */ KBD_F1, KBD_F2, KBD_F3, KBD_F4, KBD_F5,
    /* 0x3F */ 0,0,0,0,0,0,0, /* F6-F12: MSX only has F1-F5, unmapped */
    /* 0x46 */ 0, 0, 0,       /* PrintScreen, ScrollLock, Pause */
    /* 0x49 */ KBD_INSERT,
    /* 0x4A */ KBD_HOME,
    /* 0x4B */ 0,             /* PageUp */
    /* 0x4C */ KBD_DELETE,
    /* 0x4D */ KBD_STOP,      /* End -> MSX STOP (common emulator convention) */
    /* 0x4E */ 0,             /* PageDown */
    /* 0x4F */ KBD_RIGHT,
    /* 0x50 */ KBD_LEFT,
    /* 0x51 */ KBD_DOWN,
    /* 0x52 */ KBD_UP,
    /* 0x53 */ 0,             /* NumLock */
    /* 0x54 */ '/', '*', '-', '+',
    /* 0x58 */ KBD_ENTER,     /* Keypad Enter */
    /* 0x59 */ KBD_NUMPAD1, KBD_NUMPAD2, KBD_NUMPAD3, KBD_NUMPAD4,
    /* 0x5D */ KBD_NUMPAD5, KBD_NUMPAD6, KBD_NUMPAD7, KBD_NUMPAD8,
    /* 0x61 */ KBD_NUMPAD9, KBD_NUMPAD0,
    /* 0x63 */ '.',           /* Keypad . */
    /* 0x64 */ 0,             /* Non-US \| : layout dependent */
    /* 0x65 */ 0,             /* Application/Menu key - unmapped */
    /* 0x66 */ 0, 0,
};

#endif
