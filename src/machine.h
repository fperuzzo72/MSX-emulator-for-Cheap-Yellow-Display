#ifndef MACHINE_H
#define MACHINE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* What a machine has to provide to the board.
 *
 * Everything under src/device/ is the CYD: the panel, the amplifier, the
 * BLE keyboard, the card, the serial console, the boot menu. It knows
 * nothing about MSX or Spectrum. Everything under src/msx/ and
 * src/spectrum/ is a machine, and knows nothing about this board.
 *
 * A table of function pointers rather than a set of link-time symbols,
 * because one firmware carries both machines and picks at boot. The cost
 * is one indirection on calls that happen at most once a frame; nothing
 * in an inner loop goes through here. */

typedef struct Machine {
    /* For the boot menu and the banner, e.g. "MSX1 (Hotbit HB-8000)". */
    const char *name;

    /* Claim the framebuffer before anything else fragments the one big
     * DRAM region this chip has. Called before run(). */
    int (*prealloc_video)(void);

    /* Bring the machine up and run it. Never returns. */
    void (*run)(void);

    /* Non-zero once it has claimed all the memory it needs, so the BLE
     * stack knows when it is safe to allocate. */
    int (*ready)(void);

    unsigned long (*frames)(void);

    /* --- keyboard: the board carries the keys, the machine means them --- */
    void (*hid_report)(const uint8_t report[8]);
    int  (*type)(const char *text);   /* synthetic typing, for the console */
    int  (*typing)(void);

    /* --- reading the machine back over the serial console -------------- */
    int  (*screen_row)(int row, uint8_t *out, int max);
    const char *(*screen_mode_name)(void);
    int  (*char_pattern)(int code, uint8_t *rows8);
    int  (*peek)(int addr);

    /* --- sound --------------------------------------------------------- */
    void (*set_sound)(int on);
    int  (*sound_on)(void);

    /* --- what this machine can boot into -------------------------------
     * Entry 0 is always the machine on its own - BASIC, an empty slot.
     * The rest are whatever ROMs are built into this firmware. */
    int         (*entry_count)(void);
    const char *(*entry_name)(int i);
    void        (*select_entry)(int i);
    int         (*selected_entry)(void);

    /* Swap to another entry while running, without rebooting the board:
     * insert a different cartridge, put in a different tape. Resets the
     * emulated machine, which is what inserting either would do. */
    void        (*switch_to)(int entry);

    /* --- console commands only this machine has ------------------------ */
    int         (*debug_command)(const char *line);
    const char *(*debug_help)(void);
} Machine;

/* Every machine in this firmware, and the one that was chosen. */
extern const Machine *const machine_list[];
extern const int machine_count;
extern const Machine *machine;

/* Bring the choice storage up. Must be called before anything reads a
 * remembered choice. */
void machine_storage_init(void);

/* Pick one, by index into machine_list. Remembered in NVS. */
void machine_choose(int index, int entry);
int  machine_chosen_index(void);

#ifdef __cplusplus
}
#endif
#endif
