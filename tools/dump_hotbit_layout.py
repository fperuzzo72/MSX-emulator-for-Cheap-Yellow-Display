#!/usr/bin/env python3
"""Print the Hotbit HB-8000 key matrix as its own BIOS decodes it.

The BIOS carries two 48-byte tables - unshifted, then shifted - covering
matrix rows 0 to 5, eight contacts per row. This is where the layout
table in src/msx_keys.c came from; run this against a ROM image to check
it rather than trusting the copy.

  python3 tools/dump_hotbit_layout.py hotbi13p/hotbit13p.rom

0xFF marks a dead key. Which accent each of the three dead positions
carries is NOT in the ROM - see docs/KEYBOARD.md for how that was
settled on the hardware.
"""
import sys

TABLE_OFFSET = 0xDA5  # start of the unshifted table

INTL = ["0","1","2","3","4","5","6","7",
        "8","9","-","=","\\","[","]",";",
        "\'","`",",",".","/","_","A","B",
        "C","D","E","F","G","H","I","J",
        "K","L","M","N","O","P","Q","R",
        "S","T","U","V","W","X","Y","Z"]

def show(b):
    if b == 0xFF: return "<dead>"
    if 0x20 <= b < 0x7F: return repr(chr(b))
    return "0x%02X" % b

def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    rom = open(sys.argv[1], "rb").read()
    if len(rom) != 32768:
        sys.exit("expected a 32768-byte MSX1 BIOS+BASIC image")
    uns = rom[TABLE_OFFSET:TABLE_OFFSET + 48]
    shf = rom[TABLE_OFFSET + 48:TABLE_OFFSET + 96]

    print("row  bit   unshifted  shifted    (international MSX here)")
    for i in range(48):
        mark = "   <-- dead key" if uns[i] == 0xFF or shf[i] == 0xFF else ""
        print(" %d   0x%02X  %-10s %-10s %s%s"
              % (i // 8, 1 << (i % 8), show(uns[i]), show(shf[i]), INTL[i], mark))

main()
