/* msx_display.h
 *
 * Replaces esplay-fMSX's odroidGo/LibOdroidGo.h + drivers/display/display.h
 * for the FNK0103 3.5" ST7796 (320x480) panel, driven via TFT_eSPI.
 *
 * The MSX video core (video/AVideo.i, vendored from ESPlay-fMSX / fMSX-go)
 * renders into an 8bpp indexed framebuffer (msxFramebuffer) sized WIDTH x
 * HEIGHT, then calls display_write_frame_msx() once per frame to blit it to
 * the physical screen through a 16-entry (XPal) or 256-entry (BPal) RGB565
 * palette. That function is implemented in src/display_bridge.cpp using
 * TFT_eSPI, not here - this header only supplies the geometry constants and
 * its declaration.
 */
#ifndef MSX_DISPLAY_H
#define MSX_DISPLAY_H

#include <stdint.h>

/* pixel typedef comes from EMULib.h (unsigned short, since AVideo.i sets
 * BPP16 before including it) - not redeclared here to avoid a duplicate
 * typedef across translation units. */

/* MSX1 native picture is 256x192 visible; fMSX pads to 256x212/216 lines so
 * the whole 8bpp framebuffer size is round. */
#define WIDTH   256
#define HEIGHT  216

/* FNK0103 3.5" panel run in landscape (TFT_eSPI setRotation(1) or (3)). */
#define WIDTH_OVERLAY   480
#define HEIGHT_OVERLAY  320

/* Center the 256x212 MSX picture on the 480x320 panel. */
#define MSX_DISPLAY_X ((WIDTH_OVERLAY-WIDTH)/2)
#define MSX_DISPLAY_Y ((HEIGHT_OVERLAY-212)/2)

#define CURSOR_MAX_WIDTH  (22)
#define CURSOR_MAX_HEIGHT (22)

#ifdef __cplusplus
extern "C" {
#endif

/* Blit an 8bpp indexed image (or, if buffer==NULL, a flat bgColor fill) of
 * size width x height at (left,top) into the panel, translating through
 * palette[]. Implemented in src/display_bridge.cpp on top of TFT_eSPI. */
void display_write_frame_msx(short left, short top, short width, short height,
                              const uint8_t *buffer, uint16_t bgColor,
                              const uint16_t *palette);

/* Carry out any panel change the serial console asked for. Called at the
 * top of a frame, on the emulation task, because TFT_eSPI may only be
 * driven from one task. */
void display_service(void);
int  display_take_repaint(void);

/* Fill the whole panel, surround included. */
void display_fill_panel(uint16_t color);
unsigned long display_full_repaints(void);

/* Picture scale: 1 = one panel pixel per MSX pixel, 2 = three panel
 * pixels per two MSX pixels (1.5x, nearly full screen). */
void display_set_scale(int scale);
int  display_get_scale(void);

/* Claim the framebuffer before the emulator allocates its RAM - see the
 * comment on the definition in video/AVideo.i. Returns non-zero on
 * success. Defined there, called from setup(). */
int PreallocVideo(void);

/* Defined in video/AVideo.i (via video_glue.c). Allocates the 8bpp
 * framebuffer + palettes and the FreeRTOS task that flushes frames to the
 * panel. Called from platform_glue.c InitMachine()/TrashMachine(). */
int InitVideo(void);
void TrashVideo(void);

#ifdef __cplusplus
}
#endif

#endif
