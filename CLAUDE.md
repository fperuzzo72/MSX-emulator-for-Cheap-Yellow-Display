# CLAUDE.md

Context for Claude Code (or any assistant) picking up this project. Read
`README.md` first for user-facing details (build/flash/pairing/SD layout);
this file is about *why* things are built the way they are, and what's
still open.

## What this is

Emulator firmware for the Freenove FNK0103 3.5" ESP32 display board (the
"Cheap Yellow Display": plain ESP32-WROOM-32E, ST7796 SPI TFT, resistive
touch, microSD), with input from a BLE keyboard rather than the
touchscreen. PlatformIO + Arduino.

**Two machines are built from this tree**, one firmware each:

- `pio run -e msx` - MSX1, an Epcom Hotbit HB-8000. **Running on
  hardware**: boots a dumped BIOS into MSX-BASIC with no SD card, sound
  generated, BLE keyboard with US-International accents, cartridges from
  flash. 20-30 fps depending on scale.
- `pio run -e spectrum` - ZX Spectrum 48K. **Compiles; has never run.**
  There is no Spectrum ROM on this machine and the board was unplugged
  when it was written. Do not describe it as working.

The split that makes this possible: `src/device/` is the board and knows
nothing about what is emulated, `src/msx/` and `src/spectrum/` are
machines and know nothing about the board, and `src/machine.h` is the only
thing that crosses. `lib/z80/` is the CPU, shared; `lib/fmsx_core/` is the
rest of fMSX and is MSX-only, excluded from the Spectrum build by
`lib_ignore`.

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
  remove a touch-driven virtual keyboard overlay, and rewritten to render
  one 24-line band at a time instead of keeping a whole frame (see
  docs/MEMORY.md for why). `display_bridge.cpp` is the one file that talks
  to TFT_eSPI, and it owns where the picture lands and at what scale.
  **An earlier note here claimed TFT_eSPI wants normal RGB565 order and
  that the byte swap was dropped. That was wrong and it cost hours**:
  `pushPixels` streams the buffer raw and needs `setSwapBytes(true)`. See
  docs/DISPLAY.md, which collects that and the other two display faults.
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
- **SD card** (`src/sd_mount.cpp`): via ESP-IDF's
  `esp_vfs_fat_sdspi_mount` (not Arduino's `SD.h`) so the vendored core's
  plain `fopen()`/`fread()` on hardcoded `/sdcard/msx/...` paths work
  unmodified. **Not mounted at boot** - see open item 3. Two things here
  are load-bearing: the host must be set to `SPI3_HOST` explicitly,
  because the default is the display's bus and taking it blanks the panel
  with no error at all, and `max_files` must stay small, because FATFS
  keeps a 4kB cache per open file.
- **CI** (`.github/workflows/build.yml`): builds the `fnk0103` PlatformIO
  env on every push/PR and uploads `firmware.bin` +
  `bootloader.bin`/`partitions.bin` as an artifact, so a full ESP32
  toolchain is never required locally - flash the artifact with `esptool`
  alone. This exists because this project's firmware was never actually
  compiled in the sandbox that originally wrote it (no toolchain
  download there) - GitHub Actions is the first real compile pass, so
  treat early CI failures as genuinely new information, not noise.

## Known open items / likely next requests

1. **Speed.** ~42 fps at 1:1 and ~25-33 fps at the 1.5x scale, against
   the 60 a real MSX runs at. Because fMSX paces the Z80 against the
   frame, that is the whole machine running slow, not just a late picture.
   The blit is synchronous on the emulation task since the video layer
   went to a band buffer. Two ways back: overlap it with DMA (a first
   attempt with `pushPixelsDMA` and two line buffers put a flashing white
   screen up and was reverted), or give the blit its own task again and
   pay one more band of RAM. See docs/DISPLAY.md.
2. **Nobody has heard the sound.** `InitSound()` reports 22050Hz,
   `PlayAllSound()` feeds `RenderAndPlayAudio()` into the I2S built-in DAC
   (`src/audio_glue.c`), and `PLAY` runs without stalling the frame rate,
   but no sound has been confirmed coming out of the speaker. Freenove's
   own MP3 example for this board uses `AudioOutputI2S(0, 1)`, the
   internal-DAC mode, which is what that file drives.
3. **The SD card is not mounted at boot** (see the note in `setup()`).
   Nothing needs it - the BIOS is in flash - and mounting costs ~45kB,
   which with a card in the slot left the emulated VRAM 12 bytes short of
   fitting. `m 1` on the serial console mounts it on demand. Loading ROMs
   and anything MSX-DOS-shaped will have to mount it and decide what to
   give up for the memory; that is the next real design question here.
4. **No cartridge loading yet.** `ROMName[0]` still points at a fixed
   `/sdcard/msx/games/game.rom`. A ROM browser is the natural next step
   and needs item 3 settled first.
5. **No floppy, no MSX-DOS.** Would need a DISK.ROM and the card mounted
   for the life of the session.
6. **No joystick emulation** - `Joystick()`/`Mouse()` in
   `src/platform_glue.c` return 0 unconditionally. Cursor-key-as-joystick
   games still work via the keyboard.
7. **PSRAM**: settled, there is none. `esptool flash-id` reports an
   ESP32-D0WD-V3 with no embedded PSRAM and 4MB of flash. The whole memory
   budget in docs/MEMORY.md is built on that.

## The Spectrum tape, and why it is only half solved

A `.tap` is loaded by trapping the ROM's own LD-BYTES at 0x0556 and
handing over the next block whole (`src/spectrum/spectrum_tape.c`). The
trap is an opcode planted in the ROM, and this machine's ROM runs from
flash, so loading a tape first copies the ROM into RAM and patches the
copy - that is what `spectrum_tape_rom()` is for.

This works for any game that loads through the ROM and **only** for those.
Measured: Halls of the Things loads all 3 blocks and runs. Nebulus takes 4
blocks and stops at PC 0x05EE, because by then its own turbo loader has
taken over and never calls LD-BYTES again. Most commercial tapes past 1985
are in that second group.

The real fix is to stop faking the loader and emulate the signal: drive
bit 6 of port 0xFE from a generated pulse train and let whatever loader
the game brought read it. That was attempted and **does not work yet** -
kept in `src/spectrum/spectrum_tape_pulses.c.wip`, outside the build. The
decisive measurement, worth not repeating: with pulses, Halls of the
Things - which the ROM loader alone loads end to end - came back at 7
restarted blocks with PC inside LD-BYTES and a blank screen. So the fault
is in the pulse generation or its timing, not in anyone's custom loader.
Suspects, in order: the level/edge semantics of `spectrum_tape_ear()`, the
PAUSE-to-PILOT transition, and the motor-idle-stop path resetting the next
edge. The clock is not a suspect: `ExecZ80` does set and decrement
`ICount`, so frames*69888 + (69888 - ICount) is sound.

## Licensing (do not relax this casually)

`LICENSE` at the repo root is MIT but explicitly scoped (see its "NOTE ON
SCOPE" section) to only the original glue code - not the vendored
`lib/fmsx_core/` core, which is under Marat Fayzullin's fMSX/EMULib
license (personal-use-only, not commercial-redistribution-friendly). Full
texts for every vendored/adapted piece are in `third_party_licenses/`.
Keep this distinction intact in any future README/LICENSE edits.
