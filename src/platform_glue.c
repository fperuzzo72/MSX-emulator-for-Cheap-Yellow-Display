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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "MSX.h"
#include "Sound.h"
#include "ble_keyboard.h"
#include "msx_keys.h"
#include "msx_display.h"

/* fMSX/MSX.c (LoadFile(), CMOS handling) references this extern global;
 * the reference-platform ports (odroidGo/files.c) define it as their SD
 * working directory. We do the same, matching our SD layout. */
char *fullCurrentDir = "/sdcard/msx/games";

extern volatile int MSXMemoryClaimed;   /* msx_bridge.c */
extern volatile unsigned int MSXFrames; /* msx_bridge.c */

int InitMachine(void) {
    return InitVideo();
}

void TrashMachine(void) {
    TrashVideo();
}

/** Keyboard() ************************************************/
/** Called once per frame, at scanline 192, which is also when */
/** the BIOS scans the matrix. ble_keyboard_poll() services the */
/** BLE connection and hands over the latest HID report;        */
/** msx_keys_frame() turns that into this machine's matrix.     */
/*****************************************************************/
void Keyboard(void) {
    /* First frame: the machine is up and everything it needed off the heap
     * is claimed, so whatever else wants a big block (the BLE stack) can
     * stop waiting. */
    MSXMemoryClaimed = 1;
    MSXFrames++;

    ble_keyboard_poll();
    msx_keys_frame();

    /* Hand the core back for a tick, once per frame. The emulation task
     * and the video task both sit at priority 5 on core 1 and neither
     * sleeps on its own, so without this the Arduino loop task never runs
     * and the idle task never gets to feed the watchdog. One millisecond
     * out of a 16.7ms frame is a price worth paying for that. */
    vTaskDelay(1);
}

/** Joystick()/Mouse() ****************************************/
/** No physical joystick/mouse wired up on this build. Games   */
/** that support keyboard cursor-key control still work fine   */
/** via the BLE keyboard's arrow keys + space (fire).           */
/*****************************************************************/
unsigned int Joystick(void) { return 0; }
unsigned int Mouse(byte N) { (void)N; return 0; }

/** Floppy disk ***********************************************/
/** DiskPresent()/DiskRead()/DiskWrite() are already provided  */
/** by the core itself, in fMSX/Patch.c - defining them here   */
/** too is a duplicate symbol at link time. There is no floppy  */
/** on this build either way.                                   */
/*****************************************************************/

/** PlayAllSound() **********************************************/
/** Hand the core's mixed PSG/SCC/OPLL output to the DAC. fMSX   */
/** calls this once per frame with the number of microseconds    */
/** that frame covered; RenderAndPlayAudio() turns that into     */
/** samples and calls WriteAudio() in src/audio_glue.c.          */
/*******************************************************************/
/* Mixing the PSG, SCC and OPLL for a frame's worth of samples is real CPU
 * work on a core that is already emulating a Z80 and pushing pixels: it
 * costs about a quarter of the frame rate. Worth having, worth being able
 * to turn off. */
int MSXSoundOn = 1;

void PlayAllSound(int uSec) {
    if (!MSXSoundOn) return;
    RenderAndPlayAudio((unsigned int)((long long)uSec * GetSndRate() / 1000000));
}
