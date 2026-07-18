/* main.cpp - FNK0103 MSX1 emulator, entry point.
 *
 * Boot sequence:
 *   1. Mount the SD card (game ROMs + optional BIOS override live there).
 *   2. Init the ST7796 display.
 *   3. Start the BLE keyboard host (connects opportunistically in the
 *      background - you don't have to wait for it before the emulator
 *      starts, but nothing will respond to input until it's paired).
 *   4. Hand off to fMSX's StartMSX(), which runs the whole emulation
 *      (CPU, video, keyboard polling) in a blocking loop - so we run it
 *      on its own FreeRTOS task with a generous stack instead of from
 *      Arduino's loop().
 *
 * Game ROM: drop a single MSX1 cartridge ROM at /sdcard/msx/games/game.rom
 * (see README). C-BIOS (our default BIOS) is cartridge-only - it does not
 * run MSX-BASIC, so without a game ROM present you'll just get a blank/
 * "no cartridge" screen, which is expected.
 */
#include <Arduino.h>

extern "C" {
#include "MSX.h"
}

#include "sd_mount.h"
#include "ble_keyboard.h"

void display_bridge_init(); /* display_bridge.cpp */

static const char *kGameRomPath = "/sdcard/msx/games/game.rom";

static void msxTask(void *arg) {
    (void)arg;

    ROMName[0] = kGameRomPath; /* fMSX loads this as a cartridge in slot 0 if it exists */

    Mode = MSX_MSX1 | MSX_NTSC; /* Brazilian MSX machines ran PAL-M: PAL color, but 60Hz/NTSC timing. Switch to MSX_PAL if you find games run better at 50Hz. */
    RAMPages = 4;   /* 64KB RAM (16KB pages) - safe default, plenty for MSX1 titles */
    VRAMPages = 1;  /* 16KB VRAM - correct for MSX1 (TMS9918-compatible V9938 subset) */

    Serial.println("Starting MSX1 core (C-BIOS unless /sdcard/msx/bios/MSX.ROM is present)...");
    if (!StartMSX(Mode, RAMPages, VRAMPages)) {
        Serial.println("StartMSX() failed to initialize - check Serial log above.");
    }
    /* StartMSX() only returns when the emulation itself exits (ExitNow),
     * which nothing in this build currently triggers - it runs forever. */
    TrashMSX();
    vTaskDelete(NULL);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n\nFNK0103 MSX1 + BLE keyboard");

    display_bridge_init();
    sd_mount_init(); /* ok to fail: falls back to embedded C-BIOS, but no games without a card */
    ble_keyboard_init();

    /* fMSX's Z80 core + V9938 rendering want a real stack; Arduino's
     * default loop task stack (8KB) is too tight. Run it on its own task. */
    xTaskCreatePinnedToCore(msxTask, "msx", 32768, NULL, 5, NULL, 1);
}

void loop() {
    /* Everything happens in msxTask + the NimBLE host task. Just idle. */
    delay(1000);
}
