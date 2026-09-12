# The panel

The board's 3.5" ST7796 is driven through TFT_eSPI with Freenove's own pin
settings, copied verbatim from
`Libraries/FNK0114N_3.5inch_ST7796/TFT_eSPI_Setups_v1.4.zip`
(FNK0103N and FNK0114N are the same hardware under different product
codes), 80MHz SPI on HSPI included.

## Three bugs that all looked the same from the outside

None of this code had ever run before it ran on the device, and all three
of these presented as "the screen is wrong". They were found by diffing
against `CYD-MicroBASIC-MicroWriter`, which drives this same panel and
works.

**The SD card was stealing the display's bus.** `SDSPI_HOST_DEFAULT()`
returns `SPI2_HOST` on the ESP32, and SPI2 *is* HSPI - the bus TFT_eSPI
uses here (`USE_HSPI_PORT`). Mounting the card reassigned the pin matrix
from the panel's pins to the card's, and the display went dark the moment
`sd_mount_init()` ran: backlight on, nothing drawn, no error anywhere,
because the mount itself was perfectly happy. The card is wired on VSPI on
this board, so `host.slot` is now set to `SPI3_HOST` explicitly. **Never
let this default stand.**

**The panel's native size was wrong.** `TFT_eSPI tft = TFT_eSPI(480, 320)`
tells the library the panel is natively landscape. It is a 320x480
portrait panel that `setRotation(1)` turns landscape, so every
`setAddrWindow` was computed against the wrong geometry. Default-construct
it, the way the working driver does.

**`setSwapBytes(true)` was never called.** TFT_eSPI's drawing primitives
put colour bytes on the wire in the panel's order, but `pushPixels` and
`pushImage` stream the array raw and only swap when `_swapBytes` is set,
which it is not by default. Every colour pushed from a buffer went out
byte-reversed.

The serial console keeps the instruments that separate these: `x <n>`
draws test patterns (solid colours, a white block where the picture goes,
and the same block through the emulator's own band path), and `w <0|1>`
flips the byte swap live.

## Band buffer, and what it costs

The video layer renders 24 lines at a time and pushes each band as it
completes, rather than keeping a whole 55kB frame - see docs/MEMORY.md for
why. Blitting therefore happens on the emulation task.

That costs speed: **~42 fps at 1:1**, versus the 60 a real MSX runs at.
Because fMSX paces the Z80 against the frame, this slows the whole
emulated machine, not just the picture.

An attempt to win it back by overlapping the blit with DMA
(`initDMA()` plus `pushPixelsDMA` alternating between two line buffers)
produced a flashing white screen and was reverted. It is still the right
idea; it needs doing carefully, with attention to what `pushPixelsDMA`
expects when `_swapBytes` is set, and to keeping a buffer alive until its
transfer completes. The other route is a second task blitting one band
while the emulator fills the next, which costs one more band of RAM.

## Picture size

The MSX draws 256x216 into a 480x320 panel. `z 1` puts it up 1:1, crisp
and small in the middle of the glass. `z 2` scales it 1.5x to 384x324,
nearly full screen, at the cost of every third row and column repeating,
which on a text screen reads as uneven letter widths. 1:1 is the default.

Panel changes asked for from the serial console are queued and performed
at the top of a frame, on the emulation task. TFT_eSPI must be driven from
one task only: the console runs on the other core, and drawing from it
wedged the SPI bus the first time `z` was typed.

## Sound (yes, in the display document, because it is the same lesson)

The board has an SC8002B amplifier and brings its output to a **two-pin
SP+/SP- header**. It does **not** have a speaker fitted. One has to be
connected there - a small 8-ohm speaker - before anything can be heard.

The path, from Freenove's schematic:

```
GPIO26 (DAC channel 2) -> AUDIO_IN -> SC8002B -> SP+ / SP- header
GPIO4                  -> SHUTDOWN, active LOW
```

GPIO4 still had to be pulled low; it never was, and that alone would have
kept the amplifier off. But with it fixed there was still no sound,
because there was nothing to make sound with.

The instruments on the serial console for this: `a [hz] [ms]` puts a
square wave straight into the DAC with the emulator out of the way, and
`h` reports how many samples the core has actually handed over. When that
number climbs and `a` produces nothing audible, the fault is at or after
the amplifier - which is exactly where it was.
