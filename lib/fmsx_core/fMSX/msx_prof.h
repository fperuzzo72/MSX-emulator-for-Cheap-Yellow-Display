/* msx_prof.h - where a frame's time goes, for this port only.
 *
 * The MSX side runs the Z80, draws every scanline and renders sound from
 * inside one loop in MSX.c, so "the emulator is slow" is not an actionable
 * statement until those are separated. These two calls do that, and the
 * 'q' console command prints the split.
 *
 * Not part of fMSX. The calls in MSX.c are marked NOT UPSTREAM.
 */
#ifndef MSX_PROF_H
#define MSX_PROF_H

#define MSX_PROF_VIDEO 0
#define MSX_PROF_SOUND 1

long long msx_prof_now(void);
void      msx_prof(int slot, long long since);

#endif
