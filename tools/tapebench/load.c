/* The whole path, on the host: the ROM boots, LOAD "" is typed on the key
 * matrix, frames run at 50Hz with the interrupt, and the tape is the pulse
 * generator. This is what the device does, minus the device - and it runs
 * a three-minute tape in under a second, which is why it exists.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bench.h"

/* Half-rows as the ULA sees them: a low address line selects a row, a
 * zero bit is a key held down. */
static byte KEY[8];
#define ROW_P     5   /* P O I U Y */
#define ROW_ENTER 6   /* ENTER L K J H */
#define ROW_SPACE 7   /* SPACE SYMSHIFT M N B */

byte InZ80(word Port) {
    if (!(Port & 1)) {
        byte v = 0x1F;
        for (int i = 0; i < 8; i++)
            if (!((Port >> 8) & (1 << i))) v &= KEY[i];
        return (byte)(v | spectrum_tape_ear(bench_tstate()) | 0xA0);
    }
    return 0xFF;
}

static void release(void) { memset(KEY, 0x1F, sizeof KEY); }
static void press(int row, int bit) { KEY[row] &= (byte)~(1 << bit); }

/* How much of the picture has been written to. A loaded game fills it;
 * a machine sitting in BASIC leaves it empty. Reading characters back
 * would miss it, because a game screen is not made of ROM font. */
static int pictureBytes(void) {
    int set = 0;
    for (int i = 0x4000; i < 0x5800; i++) if (bench_mem()[i]) set++;
    return set;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: load <48.rom> <tape.tap> [frames]\n"); return 2; }
    if (!bench_init(argv[1], argv[2])) return 1;
    release();

    int frames = argc > 3 ? atoi(argv[3]) : 20000;
    printf("%s, %d frames (%d seconds of tape)\n", argv[2], frames, frames / 50);

    for (int f = 0; f < frames; f++) {
        /* LOAD "" and Enter, each held the few frames a finger would. */
        switch (f) {
            case 100: press(ROW_ENTER, 3); break;                  /* J = LOAD */
            case 106: release(); break;
            case 112: press(ROW_SPACE, 1); press(ROW_P, 0); break; /* SymShift+P = " */
            case 118: release(); break;
            case 124: press(ROW_SPACE, 1); press(ROW_P, 0); break;
            case 130: release(); break;
            case 136: press(ROW_ENTER, 0); break;                  /* Enter */
            case 142: release(); break;
        }
        bench_slice(1);

        if (f % 2000 == 1999)
            printf("  frame %5d  tape %3d%%  %s  PC %04X  picture %d/6144\n",
                   f + 1, spectrum_tape_progress(),
                   spectrum_tape_playing() ? "turning" : "stopped",
                   bench_cpu()->PC.W, pictureBytes());
    }

    /* Two signs, because neither alone is enough. A game that has loaded
     * usually runs its own code, but plenty sit in the ROM's key-scan
     * waiting to be started; and a game that has drawn a screen has
     * plainly loaded, but Elite and Thrust have nearly empty ones. Take
     * either as proof and say both numbers, so a close call is visible
     * rather than hidden behind a verdict. */
    int inRam = 0;
    for (int f = 0; f < 200; f++) {
        bench_slice(1);
        if (bench_cpu()->PC.W >= 0x4000) inRam++;
    }
    int pic = pictureBytes();
    printf("\ntape %d%%, picture %d/6144, %d%% of frames running from RAM - %s\n",
           spectrum_tape_progress(), pic, inRam / 2,
           (pic > 1000 || inRam > 150) ? "LOADED" : "DID NOT LOAD");
    return (pic > 1000 || inRam > 150) ? 0 : 1;
}
