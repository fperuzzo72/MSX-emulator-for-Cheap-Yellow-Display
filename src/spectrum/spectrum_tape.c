/* spectrum_tape.c - loading a .tap without emulating a tape.
 *
 * A real Spectrum loads by listening to an audio signal, and emulating
 * that faithfully means counting T-states through the ROM's edge-detection
 * loop. The shortcut every emulator uses instead is to catch the ROM's own
 * LD-BYTES routine at 0x0556 and hand it the next block outright. That
 * works for exactly the tapes stored as .tap, which are standard-speed
 * blocks; the turbo loaders that .tzx exists to preserve are not handled
 * and are not what this carries.
 *
 * Catching it needs the ROM to be writable, because the trap is an opcode
 * planted at 0x0556, and this machine's ROM runs from flash. So when a
 * tape is selected the ROM is copied into RAM first - 16kB, which this
 * board has when a Spectrum is what it is being - and the copy is patched.
 * Without a tape the ROM stays in flash and costs nothing.
 *
 * A .tap is a queue of blocks: two bytes of length, then a flag byte, the
 * data, and a checksum. The flag says header (0x00) or data (0xFF), and
 * LD-BYTES is called with the one it wants in A.
 */
#include <string.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "z80_names.h"
#include "Z80.h"
#include "spectrum.h"

#define LD_BYTES 0x0556

extern const unsigned char *spectrum_tape_image(void);
extern int spectrum_tape_size(void);

static const unsigned char *sTape;
static int sTapeLen;
static int sPos;          /* where the next block starts */
static int sBlocks;       /* blocks handed over, so silence can be diagnosed */

int spectrum_tape_blocks(void) { return sBlocks; }
static uint8_t *sRomRam;  /* the patched copy, or NULL when running from flash */

const uint8_t *spectrum_tape_rom(void) { return sRomRam; }

/* Copy the ROM down into RAM and plant the trap. Returns 0 if there is no
 * tape to load or no room for the copy. */
int spectrum_tape_begin(const unsigned char *rom) {
    sTape = spectrum_tape_image();
    sTapeLen = spectrum_tape_size();
    sPos = 0;
    if (!sTape || sTapeLen <= 0) return 0;

    if (!sRomRam) sRomRam = (uint8_t *)heap_caps_malloc(SPEC_ROM_SIZE, MALLOC_CAP_8BIT);
    if (!sRomRam) { printf("tape: no room for a writable ROM copy\n"); return 0; }

    memcpy(sRomRam, rom, SPEC_ROM_SIZE);
    /* ED FE is the core's trap opcode; the handler below runs in its place
     * and then returns the way LD-BYTES would have. */
    sRomRam[LD_BYTES]     = 0xED;
    sRomRam[LD_BYTES + 1] = 0xFE;

    printf("tape: %d bytes queued, ROM copied to RAM and trapped at %04X\n",
           sTapeLen, LD_BYTES);
    return 1;
}

/* Hand the next block to LD-BYTES, as if the tape had just played it.
 *
 * In: A the wanted flag byte, IX the destination, DE the length, carry
 * set to load rather than verify. Out: carry set if it worked. */
static void loadBlock(Z80 *R) {
    int block, len, data, i;
    byte flag, want = R->AF.B.h;
    word dest = R->IX.W, wanted = R->DE.W;

    R->AF.B.l &= (byte)~C_FLAG;          /* assume failure */

    if (!sTape || sPos + 2 > sTapeLen) return;

    block = sPos;
    len   = sTape[block] | (sTape[block + 1] << 8);
    if (len < 2 || block + 2 + len > sTapeLen) return;

    flag = sTape[block + 2];
    data = block + 3;                    /* past the length and the flag */
    sPos = block + 2 + len;              /* this block is spent either way */

    /* The wrong kind of block is not an error, it is a block for someone
     * else: the ROM simply asks again for the next one. */
    if (flag != want) return;

    len -= 2;                            /* the flag and the checksum */
    if (len > (int)wanted) len = (int)wanted;

    for (i = 0; i < len; i++)
        WrZ80((word)(dest + i), sTape[data + i]);

    R->IX.W = (word)(dest + len);
    R->DE.W = (word)(wanted - len);
    R->AF.B.l |= C_FLAG;
    sBlocks++;
}

/* The core calls this where the ED FE sits, with PC already past it. */
void spec_PatchZ80(Z80 *R) {
    if (R->PC.W != LD_BYTES + 2) return;

    loadBlock(R);

    /* LD-BYTES ends in a RET, and so must we. */
    R->PC.B.l = RdZ80(R->SP.W);
    R->PC.B.h = RdZ80((word)(R->SP.W + 1));
    R->SP.W += 2;
}
