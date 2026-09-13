/* Call the ROM's LD-BYTES directly and see whether it accepts the signal.
 *
 * The narrowest test there is: no BASIC, no interrupts, no motor
 * heuristics worth speaking of - just the loader, the generator, and the
 * question of whether one will read the other. If this fails, the signal
 * is wrong. If it passes and a real load still fails, the fault is
 * somewhere else and this says where not to look.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "Z80.h"
#include "bench.h"

static long gReads;

byte InZ80(word Port) {
    if (!(Port & 1)) {
        gReads++;
        /* No keys held: bits 0-4 high. The tape owns bit 6. */
        return (byte)(0x1F | spectrum_tape_ear(bench_tstate()) | 0xA0);
    }
    return 0xFF;
}

/* LD-BYTES wants IX where it goes, DE how long, A the expected flag byte
 * (0x00 header, 0xFF data) and carry set for LOAD rather than VERIFY. It
 * returns with carry set if the block arrived intact. */
static int ldBytes(word ix, word de, byte flag, const char *what) {
    Z80 *cpu = bench_cpu();
    ResetZ80(cpu);
    cpu->IX.W = ix; cpu->DE.W = de;
    cpu->AF.B.h = flag; cpu->AF.B.l = C_FLAG;
    cpu->SP.W = 0xFF00;
    bench_mem()[0xFF00] = 0xFE; bench_mem()[0xFF01] = 0xFF;
    bench_mem()[0xFFFE] = 0x76;      /* HALT, so the run stops on return */
    cpu->PC.W = 0x0556;
    cpu->IFF = 0;
    gReads = 0;

    /* Generous: a 40kB block at tape speed is about four minutes. */
    for (long long spent = 0; spent < 1200000000LL && !(cpu->IFF & IFF_HALT); )
        spent += bench_slice(0);

    int ok = (cpu->IFF & IFF_HALT) && (cpu->AF.B.l & C_FLAG);
    printf("  %-6s %s  carry %d  (%ld port reads)\n",
           what, ok ? "LOADED  " : "REJECTED", (cpu->AF.B.l & C_FLAG) ? 1 : 0, gReads);
    return ok;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: loader <48.rom> <tape.tap>\n"); return 2; }
    if (!bench_init(argv[1], argv[2])) return 1;
    printf("%s\n", argv[2]);

    if (!ldBytes(0x5000, 17, 0x00, "header")) return 1;
    {
        byte *h = bench_mem() + 0x5000;
        word len = (word)(h[11] | (h[12] << 8));
        word dst = (word)(h[13] | (h[14] << 8));
        char nm[11];
        memcpy(nm, h + 1, 10); nm[10] = 0;
        printf("  header: type %d \"%s\", %u bytes to %04X\n", h[0], nm, len, dst);
        if (dst < 0x4000) dst = 0x8000;   /* a BASIC block: park it in RAM */
        return ldBytes(dst, len, 0xFF, "data") ? 0 : 1;
    }
}
