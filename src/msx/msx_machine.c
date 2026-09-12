/* msx_machine.c - the MSX seen through the machine interface.
 *
 * Thin on purpose: it forwards to msx_bridge.c and msx_keys.c, which are
 * the code that was actually brought up and verified on hardware. The
 * point of this file is that src/device/ can talk to an MSX or a Spectrum
 * without knowing which.
 */
#include <string.h>
#include <stdio.h>

#include "machine.h"
#include "msx_bridge.h"
#include "msx_keys.h"

const char *machine_name(void) { return "MSX1 (Hotbit HB-8000)"; }

int  machine_prealloc_video(void) { return msx_video_prealloc(); }
void machine_run(void)            { msx_keys_init(); msx_run(); }
int  machine_ready(void)          { return msx_memory_claimed(); }
unsigned long machine_frames(void){ return msx_frame_count(); }

void machine_hid_report(const uint8_t report[8]) { msx_keys_set_report(report); }
int  machine_type(const char *text)              { return msx_keys_type(text); }
int  machine_typing(void)                        { return msx_keys_typing(); }

int machine_screen_row(int row, uint8_t *out, int max) {
    return msx_screen_row(row, out, max);
}

const char *machine_screen_mode_name(void) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "SCREEN %d", msx_screen_mode());
    return buf;
}

int machine_char_pattern(int code, uint8_t *rows8) {
    return msx_char_pattern(code, rows8);
}

int machine_peek(int addr)      { return msx_peek(addr); }
void machine_set_sound(int on)  { msx_set_sound(on); }
int  machine_sound_on(void)     { return msx_sound_on(); }

/* Commands that only make sense on this machine: the dead-key probe and
 * the raw matrix press, both of which exist because the Hotbit's keyboard
 * had to be mapped out by pressing keys and reading the result back. */
const char *machine_debug_help(void) {
    return "  d <n> <shift> <char>   press dead key n then a character\n"
           "  p <row> <bit> <mods>   press one matrix position (mods: 1 Shift, 2 Ctrl)";
}

int machine_debug_command(const char *line) {
    int a = 0, b = 0, c = 0;
    char ch = 'a';

    switch (line[0]) {
        case 'd':
            if (sscanf(line + 1, "%d %d %c", &a, &b, &ch) >= 1) {
                msx_keys_probe_dead(a, b, ch);
                printf("dead-key probe: position %d, shift %d, base '%c'\n", a, b, ch);
            }
            return 1;
        case 'p':
            if (sscanf(line + 1, "%i %i %i", &a, &b, &c) >= 2) {
                msx_keys_press_matrix(a, b, c);
                printf("pressed row %d bit 0x%02X mods %d (1=Shift 2=Ctrl)\n", a, b, c);
            }
            return 1;
        default:
            return 0;
    }
}
