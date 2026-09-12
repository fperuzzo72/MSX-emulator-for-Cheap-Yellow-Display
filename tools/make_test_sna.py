#!/usr/bin/env python3
"""Build a .sna that proves the snapshot loader works, with no game needed.

The RAM image is written here rather than by the Z80: the screen already
contains a line of text, drawn with the glyphs from a real Spectrum ROM,
and the program is nothing but a halt loop. So if the loader restores RAM
and takes the program counter off the stack correctly, that text is on
screen the instant the machine starts, and the serial console can read it
back.

  python3 tools/make_test_sna.py roms/TK95.ROM /tmp/test.sna
"""
import sys

FONT = 0x3D00
TEXT = "SNAPSHOT OK"


def screen_addr(y, x_byte):
    return ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2) | x_byte


def main():
    rom = open(sys.argv[1], "rb").read()
    ram = bytearray(49152)          # 0x4000..0xFFFF

    # Draw the text on the top line, straight into the display file.
    for col, ch in enumerate(TEXT):
        glyph = rom[FONT + (ord(ch) - 32) * 8: FONT + (ord(ch) - 32) * 8 + 8]
        for row in range(8):
            ram[screen_addr(row, col)] = glyph[row]

    # Black ink on white paper, everywhere.
    for i in range(768):
        ram[0x1800 + i] = 0x38

    # The program: JR $. Interrupts stay off so nothing redraws over it.
    ram[0x8000 - 0x4000] = 0x18
    ram[0x8001 - 0x4000] = 0xFE

    # The snapshot's program counter lives on its stack.
    sp = 0x7FFE
    ram[sp - 0x4000] = 0x00
    ram[sp + 1 - 0x4000] = 0x80

    header = bytearray(27)
    header[19] = 0x00       # IFF2 clear: interrupts disabled
    header[23] = sp & 0xFF
    header[24] = sp >> 8
    header[25] = 1          # IM 1
    header[26] = 2          # red border, so it is obvious at a glance

    open(sys.argv[2], "wb").write(bytes(header) + bytes(ram))
    print("wrote %s" % sys.argv[2])


main()
