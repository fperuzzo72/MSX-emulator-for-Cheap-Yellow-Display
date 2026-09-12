#ifndef Z80_NAMES_H
#define Z80_NAMES_H

/* The Spectrum's own copy of the Z80, under its own names.
 *
 * The CPU core asks the machine for RdZ80, WrZ80, InZ80, OutZ80,
 * LoopZ80 and PatchZ80 by those exact names. With both machines in one
 * firmware there would be two of each and nothing would link, so the
 * Spectrum compiles a second copy of the core with every one of its
 * symbols prefixed. The MSX keeps the plain names, because fMSX is full
 * of them and is better left alone.
 *
 * Cost: about 25kB of flash for the duplicated core, and nothing at all
 * at run time - no dispatch, no indirection, each machine calls straight
 * into its own copy.
 */
#define RdZ80    spec_RdZ80
#define WrZ80    spec_WrZ80
#define InZ80    spec_InZ80
#define OutZ80   spec_OutZ80
#define LoopZ80  spec_LoopZ80
#define PatchZ80 spec_PatchZ80
#define DebugZ80 spec_DebugZ80
#define ResetZ80 spec_ResetZ80
#define ExecZ80  spec_ExecZ80
#define IntZ80   spec_IntZ80
#define RunZ80   spec_RunZ80

#endif
