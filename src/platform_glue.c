/* platform_glue.c
 *
 * Implements the "TO BE WRITTEN BY USER" platform hooks that fMSX's core
 * (lib/fmsx_core/fMSX/MSX.c) calls into: machine init/teardown, keyboard
 * polling, joystick/mouse (stubbed - no joystick on this build), disk
 * access (stubbed - C-BIOS doesn't support floppy anyway, see README),
 * and sound (stubbed for now, see README "Known limitations").
 *
 * Plain C, compiled as part of the fmsx_core library alongside MSX.c so
 * it shares C linkage with it (KBD_SET/KBD_RES are macros operating on
 * KeyState[], defined in MSX.c).
 */
#include "MSX.h"
#include "ble_keyboard.h"
#include "msx_display.h"

/* fMSX/MSX.c (LoadFile(), CMOS handling) references this extern global;
 * the reference-platform ports (odroidGo/files.c) define it as their SD
 * working directory. We do the same, matching our SD layout. */
char *fullCurrentDir = "/sdcard/msx/games";

int InitMachine(void) {
    return InitVideo();
}

void TrashMachine(void) {
    TrashVideo();
}

/** Keyboard() ************************************************/
/** Scan the keyboard and update KeyState[]. Real work (BLE   */
/** report -> KBD_SET/KBD_RES) happens in ble_keyboard.c;      */
/** this just drives it once per emulated frame.               */
/*****************************************************************/
void Keyboard(void) {
    ble_keyboard_poll();
}

/** Joystick()/Mouse() ****************************************/
/** No physical joystick/mouse wired up on this build. Games   */
/** that support keyboard cursor-key control still work fine   */
/** via the BLE keyboard's arrow keys + space (fire).           */
/*****************************************************************/
unsigned int Joystick(void) { return 0; }
unsigned int Mouse(byte N) { (void)N; return 0; }

/** DiskPresent()/DiskRead()/DiskWrite() ***********************/
/** No floppy disk emulation: C-BIOS (our default BIOS) has no  */
/** DISK BIOS anyway, and this build targets ROM cartridges     */
/** loaded from the SD card, not .dsk floppy images.            */
/*****************************************************************/
byte DiskPresent(byte ID) { (void)ID; return 0; }
byte DiskRead(byte ID, byte *Buf, int N) { (void)ID; (void)Buf; (void)N; return 0; }
byte DiskWrite(byte ID, const byte *Buf, int N) { (void)ID; (void)Buf; (void)N; return 0; }

/** PlayAllSound() **********************************************/
/** Not implemented yet - see README "Known limitations". PSG/   */
/** SCC/OPLL mixing runs fine in the core regardless; this just  */
/** doesn't send it anywhere audible yet.                        */
/*******************************************************************/
void PlayAllSound(int uSec) { (void)uSec; }
