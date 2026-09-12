#ifndef BOOT_MENU_H
#define BOOT_MENU_H
#ifdef __cplusplus
extern "C" {
#endif

/* The boot menu: which machine, and which ROM to start it with.
 *
 * Driven by the touchscreen, which this board has and nothing else here
 * uses. That is not a stylistic choice: the menu has to run before any
 * machine allocates, and the BLE keyboard needs the best part of a minute
 * of the boot to pair and connect. Touch is there, it is instant, and it
 * works with no keyboard in the room.
 *
 * Called from setup() before the machine starts. Falls straight through
 * when there is only one thing to boot. */
void boot_menu_run(void);

#ifdef __cplusplus
}
#endif
#endif
