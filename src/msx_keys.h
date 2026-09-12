#ifndef MSX_KEYS_H
#define MSX_KEYS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* US-International PC keyboard -> Sharp/Epcom Hotbit HB-8000 key matrix.
 *
 * Plain C on purpose: this file is included by C++ (the BLE transport)
 * and by C (the emulator side), and must not drag in MSX.h - see the
 * `word` typedef note in msx_bridge.h. */

void msx_keys_init(void);

/* Hand over the newest 8-byte HID boot keyboard report
 * ([modifiers, reserved, key1..key6]). Cheap, just stores it. */
void msx_keys_set_report(const uint8_t report[8]);

/* Called once per emulated frame (60Hz) from the Keyboard() hook. This
 * is where the matrix is actually rebuilt. */
void msx_keys_frame(void);

/* Self-test hooks, driven over serial by debug_console.cpp, so the whole
 * mapping can be verified from this end with nothing but a USB cable and
 * no BLE keyboard in the room.
 *
 * msx_keys_type() queues a string as synthetic HID presses, which then go
 * through exactly the same US-International layer, dead keys and matrix
 * rebuild that a real keyboard would - that is the point: it tests the
 * real path, not a shortcut past it. Typing "'a" composes an a-acute,
 * because that is what those two keystrokes do on a US-International
 * keyboard. Returns how many characters it could queue. */
int  msx_keys_type(const char *text);
int  msx_keys_typing(void); /* 1 while a queued string is still going in */

/* Press one of the three dead-key positions followed by `base`, to settle
 * on real hardware which accent each position carries (the ROM's tables
 * only mark them as dead). `which` is 0, 1 or 2. */
void msx_keys_probe_dead(int which, int shift, char base);

/* Press one arbitrary matrix position, for mapping out what the BIOS
 * decodes. `row` 0-10, `bit` a single set bit. */
void msx_keys_press_matrix(int row, int bit, int shift);

/* Names the accent currently pending from a dead key, for the serial
 * console's status line. Returns "" when nothing is pending. */
const char *msx_keys_pending_accent(void);

#ifdef __cplusplus
}
#endif
#endif
