#ifndef SPECTRUM_H
#define SPECTRUM_H
#include <stdint.h>

/* ZX Spectrum 48K.
 *
 *   0x0000-0x3FFF  16kB ROM, executed straight out of flash
 *   0x4000-0x57FF  the display file, 6144 bytes of bitmap
 *   0x5800-0x5AFF  768 attribute bytes, one per 8x8 cell
 *   0x5B00-0xFFFF  the rest of the 48kB of RAM
 *
 * The ROM is not in this repo. Supply your own and build it in:
 *
 *   python3 tools/embed_rom.py 48.rom src/spectrum/spectrum_rom_data.c spectrum_rom
 *
 * Amstrad have long permitted the Spectrum ROMs to be distributed with
 * emulators, but that is their permission to give and not mine, so the
 * image stays out of the repository and out of the firmware until you put
 * it there. Without one the machine says so and stops. */

#define SPEC_ROM_SIZE   0x4000
#define SPEC_RAM_SIZE   0xC000   /* 48kB, mapped at 0x4000 */
#define SPEC_SCREEN     0x4000
#define SPEC_ATTRS      0x5800
#define SPEC_FONT_ADDR  0x3D00   /* the ROM's 8x8 font, from space */

/* 3.5MHz, 50Hz: 69888 T-states a frame. */
#define SPEC_FRAME_TSTATES 69888

/* 256x192 picture, drawn inside the same 256x216 area the panel layer
 * centres, with the border filling what is left. */
#define SPEC_PICTURE_TOP 12

void spectrum_keys_reset(void);
void spectrum_keys_hid(const uint8_t report[8]);
uint8_t spectrum_keys_read(uint8_t highAddr);
int  spectrum_keys_type(const char *text);
int  spectrum_keys_typing(void);
void spectrum_keys_frame(void);

#endif
