# The keyboard

A BLE keyboard is a physical US keyboard: it sends raw USB HID position
codes, not characters. The machine on the other side is a Brazilian Sharp
/ Epcom **Hotbit HB-8000**, whose key matrix does not match the
international MSX layout that stock fMSX assumes. So `src/msx_keys.c`
ignores fMSX's own `Keys[]` table and rebuilds the 16-byte matrix itself,
once per frame, from a table of what *this* machine's BIOS decodes each
matrix position to.

## Where the layout came from

Not from a photograph of a keyboard and not from memory: it was read out
of the BIOS ROM's own key-translation tables. They sit at offset `0xDA5`,
48 bytes of unshifted characters followed by 48 bytes of shifted ones,
covering matrix rows 0-5 (eight contacts each).
`tools/dump_hotbit_layout.py` reprints it from any ROM image.

The Hotbit differs from the international MSX layout in ways that matter:

| position | Hotbit | international MSX |
|---|---|---|
| row 0, 0x40 | `6` / `"` | `6` / `^` |
| row 1, 0x10 | `\` / `^` | `\` / `\|` |
| row 1, 0x20 | dead key | `[` / `{` |
| row 1, 0x40 | dead key / `'` | `]` / `}` |
| row 1, 0x80 | `ç` / `Ç` | `;` / `:` |
| row 2, 0x01 | dead key | `'` / `"` |
| row 2, 0x02 | `[` / `]` | `` ` `` / `~` |
| row 2, 0x04 | `,` / `;` | `,` / `<` |
| row 2, 0x08 | `.` / `:` | `.` / `>` |
| row 2, 0x20 | `<` / `>` | `_` |

`ç` has a key of its own, `;` is Shift+comma, `:` is Shift+period, and
`{`, `}`, `|`, `~` and `` ` `` do not exist on the machine at all.

## The dead keys, and how they were identified

The ROM marks three positions `0xFF` and does not say which accent each
one carries. That was settled on the hardware, not guessed: the serial
console's `d` command presses a dead key followed by a letter, `s` reads
the resulting character back out of the VDP, and `g` draws its glyph from
the pattern table. Running every combination against `a`, `e` and `u`:

| position | unshifted | shifted |
|---|---|---|
| row 1, 0x20 | acute `´` | grave `` ` `` |
| row 1, 0x40 | diaeresis `¨` | apostrophe `'` (not dead) |
| row 2, 0x01 | tilde `~` | circumflex `^` |

The decisive evidence was not the glyphs but **which letters refuse to
compose**: grave only takes an `a`, diaeresis only a `u`, circumflex takes
`a` and `e` but not `u`. That is exactly the set Portuguese uses.

## What the user gets

The keyboard behaves like **US-International**. What is printed on the
keycap is what appears on the MSX, and the dead keys compose:

- `'` then `a` → á, `'` then `e` → é
- `~` then `a` → ã, `~` then `o` → õ
- `^` then `a` → â, `^` then `e` → ê
- `` ` `` then `a` → à
- `"` then `u` → ü
- AltGr + `c` → ç (it has its own key here, so no dead key involved)

A dead key followed by something it cannot sit on gives the accent as a
character, the way it does on a PC: `"` then `S` types `"S`, `'` then
space types `'`.

## Two things worth knowing about the implementation

**The matrix is rebuilt from scratch every frame** rather than tracking
press and release deltas. The BIOS scans the matrix once per VDP
interrupt and `Keyboard()` is called at exactly that point, so a full
rebuild is both correct and far less bug-prone - the earlier version
diffed HID reports and had to special-case left and right modifiers
sharing one contact.

**Shift is faked when the machine needs a different shift state than the
one being held.** A US keyboard has `;` unshifted; the Hotbit has it on
Shift+comma. So pressing `;` holds the comma contact *and* the Shift
contact, for as long as the key is down. Keys that need no such
translation - letters, digits, arrows, space, Return - take a direct path
with no latency at all, which is the path games use.

Accents are the exception: they need two keypresses in sequence, so they
go through a small queue that holds each press for a few frames. Typing an
accented character costs about 80ms. Nothing a game needs goes near it.

## Checking it without a keyboard in the room

The serial console (115200 8N1) can drive the whole thing:

```
t 10 PRINT "S~AO PAULO"     type it, dead keys and all (\n is Return)
s                           read the emulated screen back
d <pos> <shift> <char>      press a dead key, then a character
g 0xB0                      draw a character's glyph
h                           heap, frame rate, keyboard state
```

`t` feeds synthetic HID reports through the same US-International layer,
dead keys and matrix rebuild that a real keyboard drives - it tests the
real path, not a shortcut past it.
