# FNK0103 MSX1 emulator + BLE keyboard

MSX1 emulator (fMSX/Z80 core) for the Freenove FNK0103 3.5" ESP32 display
("Cheap Yellow Display"-style board, ST7796 panel), with input from a
Bluetooth Low Energy keyboard instead of the touchscreen.

> **License note:** the `LICENSE` file in this repo (MIT) covers only the
> original glue code written for this project. The vendored MSX emulator
> core (`lib/fmsx_core/`) is under Marat Fayzullin's fMSX/EMULib license,
> which is personal-use-only, not commercial-redistribution-friendly. See
> "Licensing" below and `LICENSE`'s "NOTE ON SCOPE" section before you do
> anything beyond building this for yourself.

**Status: written and cross-checked against real, working reference
projects and verified library APIs, but NOT compiled or flashed on real
hardware.** The sandbox this was built in couldn't download the ESP32
toolchain (network restrictions), so there was no way to build-test it
here. Treat this as a strong, carefully-reasoned first draft: architecture
and pin mappings are pulled from Freenove's own files, the emulator core
is real vendored fMSX source (not reimplemented from scratch), and the
BLE HID code follows a proven working pattern - but there will likely be
at least a small compile error or two to fix on your first build. See
"If something doesn't compile" below.

## What's in the box

- **MSX1 only** (not MSX2/MSX2+) - simpler, lower memory footprint, matches
  this board's likely lack of PSRAM (see "Hardware notes").
- **C-BIOS** (open-source MSX-compatible BIOS) embedded in flash as the
  default boot ROM. C-BIOS is cartridge-only: it does **not** run
  MSX-BASIC or floppy disks. Drop your own dumped BIOS ROM on the SD card
  to override it (see "Using your own Brazilian BIOS ROM" below).
- **BLE keyboard input**, not the touchscreen. Boots up scanning for any
  BLE keyboard advertising the standard HID service and pairs with the
  first one it finds.
- **No sound yet, no floppy disk, no joystick emulation.** See "Known
  limitations."

## Hardware reference

Pulled directly from Freenove's own `Freenove_ESP32_Display` repo
(`Libraries/FNK0114N_3.5inch_ST7796/TFT_eSPI_Setups_v1.4.zip` and the
`Sketches/Sketch_06.1_SD_Test` / `Sketch_07.1_Play_MP3_SD_by_DAC`
examples) - FNK0103 and FNK0114N share the same 3.5" ST7796 board design,
just bundled under different product/firmware names.

| Function | Pins |
|---|---|
| TFT (ST7796, HSPI) | MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, RST tied to EN (-1), BL 27 |
| Touch (XPT2046, shares TFT SPI bus) | CS 33 (unused in this build) |
| microSD (VSPI, separate bus) | SCK 18, MISO 19, MOSI 23, CS 5 |
| Speaker (internal DAC / I2S) | not wired up in this build (no audio yet) |

Chip: **ESP32-WROOM-32E** (classic dual-core Xtensa LX6 @240MHz), **not**
ESP32-S3. Freenove's own schematic/datasheet bundle for this board only
references the plain WROOM-32E part, with no "R2"/"R8" PSRAM suffix
anywhere - so this build assumes **no PSRAM**. Everything here (64KB
emulated RAM, 16KB VRAM, ~54KB 8bpp framebuffer, BLE stack) is sized to
fit in the ~520KB of internal SRAM without needing PSRAM. If your board
does turn out to have PSRAM, you'll just have more headroom (useful if
you add sound or bump RAMPages later).

## Building and flashing

1. Install [PlatformIO](https://platformio.org/) (VS Code extension, or
   `pip install platformio` for the CLI).
2. Open this folder as a PlatformIO project (or `cd` into it for the CLI).
3. `pio run -t upload` (CLI) or the PlatformIO IDE's Upload button. First
   build will download the ESP32 toolchain + TFT_eSPI + NimBLE-Arduino -
   give it a few minutes.
4. `pio device monitor -b 115200` to watch the boot log (SD mount status,
   BLE connection status, any error messages).

### If something doesn't compile

The most likely failure points, in rough order of likelihood:

- **TFT_eSPI version mismatch**: the pin/driver setup is passed entirely
  via `build_flags` in `platformio.ini` (the `-D TFT_xxx` lines) rather
  than a `User_Setup.h` file, which is the officially supported way to
  configure TFT_eSPI without editing the library - but library API details
  occasionally shift between versions. If it complains about unknown
  driver/pin macros, check TFT_eSPI's own `User_Setup_Select.h` /
  `TFT_eSPI.h` in whatever version PlatformIO pulled for the exact macro
  names expected.
- **NimBLE-Arduino API drift**: `ble_keyboard.cpp` uses `NimBLEDevice`,
  `NimBLEScan`, `NimBLEClient`, `NimBLERemoteService/Characteristic` -
  these are stable APIs but method names/signatures have shifted slightly
  across NimBLE-Arduino major versions. If you hit errors here, check
  against the version PlatformIO installed (`pio pkg list`).
- **fMSX core warnings-as-errors**: the vendored core in `lib/fmsx_core`
  is decades-old C, written loosely (implicit int, old-style casts). It
  should compile fine under GCC's defaults, but if your PlatformIO/IDE
  config adds `-Werror`, turn that off for this project.

If you get stuck, paste me the exact compiler error and I'll fix it.

## SD card layout

```
/msx/bios/MSX.ROM      <- optional: your own dumped MSX1 BIOS (32KB).
                           If absent, embedded C-BIOS is used automatically.
/msx/games/game.rom    <- your MSX1 cartridge game (single-slot for now,
                           see "Known limitations")
```

Format the card FAT32. The firmware creates `/msx/bios` and `/msx/games`
automatically on first boot if they don't exist, but it can't create the
card itself - without a card, you still get C-BIOS but no game will load
(C-BIOS has nothing to boot into without a cartridge).

## Using your own Brazilian BIOS ROM

Drop your dumped ROM at `/msx/bios/MSX.ROM` on the SD card (must be
exactly 32KB / 32768 bytes, the standard MSX1 main-ROM size). The
firmware tries that file first and only falls back to the embedded
C-BIOS if it's missing - see the `LoadROM()` patch in
`lib/fmsx_core/fMSX/MSX.c` (search for "embedded fallback").

**Accented character support (ç, á, é, ã, etc.) is not wired up yet.**
fMSX's keyboard-matrix table (`Keys[]` in `MSX.c`) only covers plain
ASCII; the Brazilian ABNT2 extra keys and accent dead-keys sit on matrix
positions that differ by MSX model (Gradiente Expert, Sharp/Epcom
Hotbit, Sony HB, etc). Tell me which model your ROM dump is from and I'll
extend `Keys[]` with the right row/column bits and wire the extra HID
keycodes to them in `src/msx_keys.h`. Until then, everything else
(letters, digits, standard punctuation, function/control keys, arrows)
works normally with your ROM.

## Pairing a BLE keyboard

The firmware starts scanning for BLE HID devices immediately on boot and
connects to the first one it finds advertising the HID service (0x1812).
Put your keyboard in pairing/discoverable mode before or shortly after
power-on. There's no on-screen pairing UI yet - watch the serial monitor
for connection status.

Only **BLE** keyboards work (Apple Magic Keyboard, Logitech K380/K480,
most current wireless keyboards). Older **Bluetooth Classic** (BR/EDR)
keyboards are not supported by this build - that needs a different,
heavier stack (Bluedroid classic HID host). Say the word if you have one
of those specifically and want it added.

## Known limitations / next steps

- **No sound.** `PlayAllSound()` is stubbed out in `src/platform_glue.c`.
  The PSG/SCC/OPLL mixing still runs correctly inside the core; it's just
  not being sent anywhere. Wiring it to the board's I2S DAC output is a
  contained follow-up (see the pin table above - the speaker circuit
  exists on this board, just unused here).
- **No floppy disk / MSX-BASIC.** Inherent to using C-BIOS; would need
  your own real BIOS + DISK.ROM to change.
- **No joystick emulation**, keyboard cursor keys work in games that
  support keyboard control.
- **Single game slot** (`/msx/games/game.rom`, hardcoded path). A proper
  on-screen file browser to pick from multiple ROMs is a natural next
  step once the basics are confirmed working on your hardware.
- **Accented/Brazilian keyboard layout**: see above.
- **Not compile-tested** (see top of this file).

## Licensing (this matters if you do anything beyond personal hobby use)

The `LICENSE` file (MIT) at the repo root covers only the original code
written for this project - it is **not** a blanket license for everything
in the repo. This project vendors and adapts several other people's code.
See `third_party_licenses/` for full texts:

- `lib/fmsx_core/{Z80,fMSX,EMULib}`: Marat Fayzullin's fMSX/EMULib -
  **free for personal use, NOT for commercial redistribution** (his
  license predates OSI-style open source licenses; see
  `fmsx_and_emulib.txt`).
- `lib/fmsx_core/video/AVideo.i`: Schuemi's fMSX-go / ESPlay-fMSX video
  glue, MIT licensed.
- C-BIOS binaries (`src/cbios_data.c`): BSD-style license, freely
  redistributable.
- `src/ble_keyboard.cpp` connect pattern: adapted from esp32beans'
  BLE_HID_Client, MIT licensed.

Given the fMSX/EMULib non-commercial restriction, this project as a whole
is fine for your own personal device but shouldn't be sold or
commercially redistributed without sorting that out with Marat Fayzullin
first.
