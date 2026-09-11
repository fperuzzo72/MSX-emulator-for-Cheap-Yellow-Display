# CLAUDE.md

Context for Claude Code (or any assistant) picking up this project. Read
`README.md` first for user-facing details (build/flash/pairing/SD layout);
this file is about *why* things are built the way they are, and what's
still open.

## What this is

MSX1 emulator firmware for the Freenove FNK0103 3.5" ESP32 display board
("Cheap Yellow Display" style: plain ESP32-WROOM-32E, ST7796 SPI TFT,
resistive touch, microSD slot), with input from a BLE keyboard instead of
the touchscreen. PlatformIO + Arduino framework. Not commissioned as a
one-off script - it's meant to be built on incrementally.

**Status (2026-09-11): running on hardware.** It boots a real Sharp/Epcom
Hotbit HB-8000 BIOS into MSX-BASIC at 59-61 fps with sound, no SD card
needed - the BIOS is embedded in the firmware. Verified by driving the
machine over the serial console and reading its screen back out of the
emulated VDP; see "Checking it without a keyboard in the room" in
docs/KEYBOARD.md. The one thing not yet verified on hardware is an actual
BLE keyboard pairing, because none was in the room at the time.

## How it's built (context for future changes)

- **Emulator core** (`lib/fmsx_core/{Z80,fMSX,EMULib}`): vendored, not
  reimplemented, from Marat Fayzullin's fMSX via the `pebri86/esplay-fMSX`
  ESP32 port. Kept as close to upstream as possible - changes are
  localized and commented (search `MSX.c` for "embedded fallback" to find
  the one deliberate patch, which adds a C-BIOS fallback to `LoadROM()`).
  Do not casually "clean up" this code; it's old, terse C on purpose and
  matching upstream matters more than style here.
- **Video** (`lib/fmsx_core/video/AVideo.i` + `msx_display.h` +
  `src/display_bridge.cpp`): AVideo.i is also vendored (from the same
  esplay-fMSX project, MIT-licensed portion by Schuemi), trimmed to
  remove a touch-driven virtual keyboard overlay (~300KB framebuffer we
  don't have RAM for and don't need with a real keyboard) and to drop a
  byte-swap that was specific to that project's raw SPI driver (TFT_eSPI
  wants normal RGB565 order). `display_bridge.cpp` is the one file that
  actually talks to TFT_eSPI.
- **Keyboard**: split in two on purpose. `src/ble_keyboard.cpp` is only
  transport (NimBLE central, Boot Keyboard Input Report 0x2A22, falling
  back to generic Report 0x2A4D). Everything about what the keys *mean* -
  the US-International layer, the dead keys, this machine's matrix - is in
  `src/msx_keys.c`, which is plain C and knows nothing about BLE. The
  Hotbit's matrix does not match the international MSX layout fMSX
  assumes, so `Keys[]` goes unused and the matrix is rebuilt from scratch
  every frame. **Read docs/KEYBOARD.md before touching any of it**; the
  layout came out of the BIOS ROM's own tables and the dead keys were
  identified on the hardware.
- **The C/C++ wall** (`src/msx_bridge.h`): fMSX's `Z80.h` does
  `typedef unsigned short word` and the ESP32 Arduino core's `Arduino.h`
  does `typedef unsigned int word`. Any translation unit pulling in both
  fails to compile, and letting Arduino's 32-bit `word` win would silently
  change the layout of every fMSX structure. So **no .cpp file may include
  MSX.h or Z80.h** - C++ talks to the core only through `msx_bridge.h`,
  and the implementations live in plain-C files that never see Arduino.h.
- **Memory**: the single hardest constraint, and the reason for several
  things that look odd (the BIOS executing from flash, the video band
  buffer, EmptyRAM being a static array, the allocation ordering in
  `setup()`). **docs/MEMORY.md has the measured numbers**; do not undo any
  of it without reading that first.
- **Serial console** (`src/debug_console.cpp`): types into the emulated
  machine and reads its screen back, so the firmware can be verified over
  the USB cable with nobody watching the panel. This is how the keyboard
  layout was worked out and how it should be re-checked after changes.
- **BIOS**: the owner's own dumped Hotbit HB-8000 BIOS, embedded in flash
  by `tools/embed_rom.py` into `src/hotbit_bios_data.c`. **That file and
  the ROM it comes from are gitignored and must never be committed or
  published.** A clone without them still builds: `tools/local_bios.py`
  (a PlatformIO pre-build hook) only defines `HAVE_LOCAL_BIOS` when the
  generated file is present, and otherwise the C-BIOS in `src/cbios_data.c`
  is used - which boots cartridges but not MSX-BASIC. An `MSX.ROM` on an
  SD card still overrides both.
- **SD card** (`src/sd_mount.cpp`): mounted via ESP-IDF's
  `esp_vfs_fat_sdspi_mount` (not Arduino's `SD.h`) specifically so the
  vendored fMSX core's plain `fopen()`/`fread()` calls on hardcoded
  `/sdcard/msx/...` paths work unmodified.
- **CI** (`.github/workflows/build.yml`): builds the `fnk0103` PlatformIO
  env on every push/PR and uploads `firmware.bin` +
  `bootloader.bin`/`partitions.bin` as an artifact, so a full ESP32
  toolchain is never required locally - flash the artifact with `esptool`
  alone. This exists because this project's firmware was never actually
  compiled in the sandbox that originally wrote it (no toolchain
  download there) - GitHub Actions is the first real compile pass, so
  treat early CI failures as genuinely new information, not noise.

## Known open items / likely next requests

1. **Accented/Brazilian keyboard layout.** The owner has his own dumped
   Brazilian MSX BIOS ROM (supports ç, á, é, ã, etc.) but the exact
   matrix row/column positions for those keys depend on which Brazilian
   MSX model it's from (Gradiente Expert, Sharp/Epcom Hotbit, Sony HB all
   differ). Needs that model identified, then extend `Keys[]` in
   `lib/fmsx_core/fMSX/MSX.c` and wire the extra HID keycodes
   (0x32, 0x64, and the ABNT-specific ones) in `src/msx_keys.h`.
2. **No sound.** `PlayAllSound()` is stubbed in `src/platform_glue.c`.
   PSG/SCC/OPLL mixing already runs correctly inside the core; wiring it
   to the board's I2S/DAC output (pins noted in README) is the remaining
   work.
3. **Single hardcoded game ROM** (`/sdcard/msx/games/game.rom` in
   `src/main.cpp`). An on-screen ROM browser is the natural next step.
4. **No floppy / MSX-BASIC** - inherent to using C-BIOS; would need the
   owner's own BIOS + a DISK.ROM to lift.
5. **No joystick emulation** - `Joystick()`/`Mouse()` in
   `src/platform_glue.c` return 0 unconditionally. Cursor-key-as-joystick
   games still work via the keyboard.
6. **PSRAM uncertainty**: Freenove's own datasheet bundle for this board
   only names the plain (non-PSRAM) WROOM-32E part, so the whole memory
   budget (64KB emulated RAM + 16KB VRAM + ~54KB framebuffer + BLE stack)
   was sized to fit in ~520KB of internal SRAM without PSRAM. If real
   hardware testing shows PSRAM is actually present, there's more
   headroom available (e.g. for #2 above).

## Licensing (do not relax this casually)

`LICENSE` at the repo root is MIT but explicitly scoped (see its "NOTE ON
SCOPE" section) to only the original glue code - not the vendored
`lib/fmsx_core/` core, which is under Marat Fayzullin's fMSX/EMULib
license (personal-use-only, not commercial-redistribution-friendly). Full
texts for every vendored/adapted piece are in `third_party_licenses/`.
Keep this distinction intact in any future README/LICENSE edits.
