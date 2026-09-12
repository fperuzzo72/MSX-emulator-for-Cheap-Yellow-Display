#ifndef MACHINE_H
#define MACHINE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* What a machine has to provide to the board.
 *
 * Everything under src/device/ is the CYD: the panel, the amplifier, the
 * BLE keyboard, the card, the serial console. It knows nothing about MSX
 * or Spectrum. Everything under src/msx/ and src/spectrum/ is a machine,
 * and knows nothing about this board. This header is the only thing that
 * crosses, and it is deliberately small.
 *
 * Two firmwares are built from this tree, one per machine; platformio.ini
 * picks which machine directory goes into the build. */

/* Name for the boot banner, e.g. "MSX1 (Hotbit HB-8000)". */
const char *machine_name(void);

/* Claim the framebuffer before anything else fragments the one big DRAM
 * region this chip has. Called before machine_run(). */
int machine_prealloc_video(void);

/* Bring the machine up and run it. Never returns. */
void machine_run(void);

/* Non-zero once the machine has claimed all the memory it needs, so the
 * BLE stack knows when it is safe to allocate. */
int machine_ready(void);

/* Frames drawn since power-on, for the frame-rate readout. */
unsigned long machine_frames(void);

/* --- keyboard ---------------------------------------------------- */

/* Hand over the latest 8-byte HID boot keyboard report. The machine
 * decides what those keys mean; the board only carries them. */
void machine_hid_report(const uint8_t report[8]);

/* Type a string as if on the keyboard, for testing over the serial
 * console. Returns how many characters were accepted. */
int machine_type(const char *text);
int machine_typing(void);

/* --- reading the machine back ------------------------------------ */

/* Text-mode screen readback, so the firmware can be checked over the USB
 * cable rather than by looking at the panel. Returns columns written, or
 * 0 when the current mode has no character cells. */
int machine_screen_row(int row, uint8_t *out, int max);

/* A one-line description of the current screen mode. */
const char *machine_screen_mode_name(void);

/* The 8x8 bitmap for a character code, from wherever the machine keeps
 * its font. Returns 0 if it has none to show. */
int machine_char_pattern(int code, uint8_t *rows8);

/* Read a byte of the machine's memory. -1 when that address is not
 * readable RAM. */
int machine_peek(int addr);

/* --- sound ------------------------------------------------------- */
void machine_set_sound(int on);
int  machine_sound_on(void);

/* --- machine-specific console commands --------------------------- */

/* Handle a console line the generic console did not recognise. Returns
 * non-zero if the machine dealt with it. Lets a machine add its own
 * commands without the console growing to know about all of them. */
int machine_debug_command(const char *line);

/* What those commands are, for the help line. Empty string if none. */
const char *machine_debug_help(void);

#ifdef __cplusplus
}
#endif
#endif
