#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench.h"

static byte MEM[0x10000];
static Z80  CPU;
static long long gBase;
static int  gSlice = 69888;

Z80      *bench_cpu(void) { return &CPU; }
byte     *bench_mem(void) { return MEM; }

/* The device computes this as frames*69888 + (69888 - ICount). Here the
 * frames are accumulated in gBase, which is the same thing without the
 * frame counter. Measured monotonic across EI and interrupts. */
long long bench_tstate(void) { return gBase + (gSlice - CPU.ICount); }

long long bench_slice(int withInterrupt) {
    gSlice = 69888;
    int left = ExecZ80(&CPU, gSlice);
    long long used = gSlice - left;
    gBase += used;
    if (withInterrupt) IntZ80(&CPU, INT_IRQ);
    return used;
}

int bench_init(const char *romPath, const char *tapePath) {
    FILE *f = fopen(romPath, "rb");
    if (!f) { perror(romPath); return 0; }
    size_t n = fread(MEM, 1, 0x4000, f);
    fclose(f);
    if (n != 0x4000) { fprintf(stderr, "%s: not a 16kB ROM\n", romPath); return 0; }
    if (!tape_load(tapePath)) return 0;
    memset(&CPU, 0, sizeof CPU);
    ResetZ80(&CPU);
    return spectrum_tape_begin(0);
}

/* The device gives the tape a RAM copy of the ROM to plant its trap in.
 * Here the ROM is just memory, so it can have one - unless TRAP=0 is set
 * in the environment, which takes the shortcut away and tests the signal
 * on its own. */
uint8_t *spectrum_rom_writable(void) {
    const char *t = getenv("TRAP");
    return (t && !strcmp(t, "0")) ? 0 : MEM;
}

byte RdZ80(word A)            { return MEM[A]; }
byte OpZ80(word A)            { return MEM[A]; }
void WrZ80(word A, byte V)    { if (A >= 0x4000) MEM[A] = V; }   /* ROM is ROM */
void OutZ80(word P, byte V)   { (void)P; (void)V; }
word LoopZ80(Z80 *R)          { (void)R; return INT_NONE; }

/* With both machines in one firmware the Spectrum's half of the world is
 * compiled under prefixed names (see z80_names.h), so the tape calls
 * spec_RdZ80 while the CPU here is the plain one. Join them up. */
byte spec_RdZ80(word A)         { return RdZ80(A); }
void spec_WrZ80(word A, byte V) { WrZ80(A, V); }
void spec_PatchZ80(Z80 *R);
void PatchZ80(Z80 *R)           { spec_PatchZ80(R); }
