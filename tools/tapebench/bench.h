#ifndef BENCH_H
#define BENCH_H
#include <stdint.h>
#include "Z80.h"

/* Shared plumbing: 64kB of memory with the ROM at the bottom, a Z80, and
 * a T-state clock computed exactly the way spectrum.c computes it. */
int        bench_init(const char *romPath, const char *tapePath);
Z80       *bench_cpu(void);
byte      *bench_mem(void);
long long  bench_tstate(void);

/* Run one frame's worth of cycles and return how many were used. Pass
 * non-zero to fire the 50Hz interrupt at the end of it. */
long long  bench_slice(int withInterrupt);

/* From spectrum_tape.c, the file under test. */
uint8_t spectrum_tape_ear(long long tstate);
int     spectrum_tape_playing(void);
int     spectrum_tape_progress(void);
int     spectrum_tape_begin(const unsigned char *rom);
int     spectrum_tape_blocks(void);

uint8_t *spectrum_rom_writable(void);

int tape_load(const char *path);   /* tapefile.c */
#endif
