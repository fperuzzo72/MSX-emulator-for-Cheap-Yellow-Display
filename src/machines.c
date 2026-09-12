/* machines.c - which machines this firmware carries, and which was chosen.
 *
 * A build can carry one machine or both; platformio.ini decides by which
 * machine directories go into it, and the HAVE_* flags below follow. With
 * both present the boot menu picks, and the choice is remembered in NVS
 * so the board comes back up as whatever it was last.
 */
#include "machine.h"
#include "nvs_flash.h"
#include "nvs.h"

#ifdef HAVE_MACHINE_MSX
extern const Machine msx_machine;
#endif
#ifdef HAVE_MACHINE_SPECTRUM
extern const Machine spectrum_machine;
#endif

const Machine *const machine_list[] = {
#ifdef HAVE_MACHINE_MSX
    &msx_machine,
#endif
#ifdef HAVE_MACHINE_SPECTRUM
    &spectrum_machine,
#endif
};

const int machine_count = (int)(sizeof(machine_list) / sizeof(machine_list[0]));

/* Set during boot, before anything else touches it. */
const Machine *machine = 0;

#define NVS_NAMESPACE "cyd"
#define NVS_KEY       "machine"

int machine_chosen_index(void) {
    nvs_handle_t h;
    int32_t v = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_i32(h, NVS_KEY, &v) != ESP_OK) v = 0;
        nvs_close(h);
    }
    if (v < 0 || v >= machine_count) v = 0;
    return (int)v;
}

void machine_choose(int index, int entry) {
    nvs_handle_t h;
    if (index < 0 || index >= machine_count) index = 0;
    machine = machine_list[index];
    if (entry >= 0) machine->select_entry(entry);
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NVS_KEY, (int32_t)index);
        nvs_commit(h);
        nvs_close(h);
    }
}
