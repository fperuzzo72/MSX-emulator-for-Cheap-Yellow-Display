/* z80_spectrum.c - the Spectrum's copy of the CPU core. See z80_names.h.
 *
 * Including a .c is unusual and deliberate: it is how the same upstream
 * source becomes a second, independently named instance without editing
 * it or maintaining a fork.
 */
#include "z80_names.h"
#include "Z80.c"
