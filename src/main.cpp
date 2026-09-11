/* main.cpp - FNK0103 MSX1 emulator, entry point.
 *
 * Boot sequence:
 *   1. Display up first, so there is something on the panel even if a
 *      later step goes wrong.
 *   2. Try the SD card. Everything works without one: the BIOS is
 *      embedded in flash, and a missing card just means no cartridge
 *      images, which on a real BIOS lands you in MSX-BASIC.
 *   3. Start the BLE keyboard host. It connects in the background; the
 *      emulator does not wait for it.
 *   4. Hand off to the fMSX core on its own task with a generous stack -
 *      StartMSX() never returns, and Arduino's 8KB loop task is far too
 *      tight for the Z80 core plus the VDP renderer.
 *
 * The serial console (debug_console.cpp) runs alongside all of this and
 * can read back what the emulated machine is showing, which is how this
 * firmware gets verified without anyone having to squint at the panel.
 */
#include <Arduino.h>

#include "sd_mount.h"
#include "ble_keyboard.h"
#include "msx_bridge.h"
#include "msx_keys.h"
#include "debug_console.h"

void display_bridge_init(); /* display_bridge.cpp */

static void msxTask(void *arg) {
    (void)arg;
    Serial.printf("MSX: starting core, free heap %u, largest block %u\n",
                  msx_free_heap(), msx_largest_block());
    msx_heap_report();
    msx_run();
    Serial.println("MSX: core exited");
    vTaskDelete(NULL);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n\nFNK0103 MSX1 (Hotbit HB-8000) + BLE keyboard");
    Serial.printf("boot: free heap %u, largest block %u\n",
                  msx_free_heap(), msx_largest_block());

    display_bridge_init();
    msx_keys_init();

    /* First claim on the heap goes to the framebuffer: it is the biggest
     * single block anything here needs, and the chip has exactly one
     * region large enough to hold it. */
    if (!msx_video_prealloc())
        Serial.println("video: framebuffer allocation FAILED");

    /* Optional: with no card the embedded BIOS still boots MSX-BASIC. */
    if (!sd_mount_init()) {
        Serial.println("SD: running without a card (BIOS is embedded in flash)");
    }

    debug_console_init();

    /* 12KB: the Z80 core and the VDP renderer are iterative, not
     * recursive, and on a board with no PSRAM 20KB of unused stack is
     * 20KB the emulated machine does not get. */
    xTaskCreatePinnedToCore(msxTask, "msx", 12288, NULL, 5, NULL, 1);

    /* BLE comes up only after the emulator has taken what it needs. The
     * machine wants 64kB of RAM in one contiguous piece, and the ESP32's
     * DRAM is split into regions: if the BLE stack allocates first, its
     * blocks land in the middle of the largest region and that 64kB stops
     * existing. Waiting a moment costs nothing - the keyboard is no use
     * until there is a BASIC prompt to type at anyway. */
    for (int i = 0; i < 200 && !msx_memory_claimed(); i++) delay(25);
    Serial.printf("boot: emulator has its memory, free %u, largest block %u\n",
                  msx_free_heap(), msx_largest_block());
    ble_keyboard_init();
}

void loop() {
    /* Everything runs on its own task: the emulator and its video task on
     * core 1, NimBLE and the serial console on core 0. */
    delay(1000);
}
