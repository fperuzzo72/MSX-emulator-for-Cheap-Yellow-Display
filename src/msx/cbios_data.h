#ifndef CBIOS_DATA_H
#define CBIOS_DATA_H
#ifndef PROGMEM
#define PROGMEM
#endif
/* C-BIOS MSX1 (open source, BSD-style licensed, see /third_party_licenses/cbios.txt)
 * cbios_main_msx1_rom: 32KB MAIN BIOS+BASIC ROM image, mapped at slot0 page0-1.
 * cbios_logo_msx1_rom: 16KB boot logo ROM (optional, cosmetic startup splash).
 */
extern const unsigned char cbios_main_msx1_rom[32768] PROGMEM;
extern const unsigned char cbios_logo_msx1_rom[16384] PROGMEM;
#define CBIOS_MAIN_MSX1_SIZE 32768
#define CBIOS_LOGO_MSX1_SIZE 16384
#endif
