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

- `'` then `a` -> á, `'` then `e` -> é
- `~` then `a` -> ã, `~` then `o` -> õ
- `^` then `a` -> â, `^` then `e` -> ê
- `` ` `` then `a` -> à
- `"` then `u` -> ü
- `'` then `c` -> ç

Case follows the same rule as any other letter on this machine: **CAPS
exclusive-or Shift**. With CAPS on, `~a` gives Ã; hold Shift with it and
you get ã.

A dead key followed by something it cannot sit on gives the accent as a
character, the way it does on a PC: `"` then `S` types `"S`, and `'` then
Space types `'` - one apostrophe, no space after it.

Esc is the **STOP** key, not the machine's ESC. A PC keyboard has no
BREAK, and Ctrl+STOP is the only way to interrupt a running BASIC
program, so Ctrl+Esc breaks and Esc alone pauses a listing. The machine's
own ESC is on PageDown.

## How the accents actually get in

Not through the dead keys. That was the first design and it was wrong.

Driving the BIOS's own composition means pressing the machine's dead key
and then the letter, and this BIOS decides the case of the result from
CAPS alone - hold Shift with the letter and it drops the accent and
prints the bare letter. So Shift did nothing for accented letters and a
capital one meant reaching for CAPS first. It also cannot make a
c-cedilla at all (acute+c gives a plain c; on this keyboard ç is a key,
not a composition), and the keyboard has no key for a bare `~` or `` ` ``
even though the character set has both.

So the composition happens here instead, ported from the
US-International state machine in `CYD-MicroBASIC-MicroWriter`, and the
finished character goes straight into the BIOS's keyboard buffer -
`KEYBUF` at 0xFBF0, through `PUTPNT` at 0xF3F8 - by `msx_type_char()` in
`src/msx_bridge.c`. Those addresses were confirmed on the machine with
the console's `r` command, not taken on faith.

Everything else still goes through the key matrix, where games expect to
find it, and at no cost in latency.

### The character codes

Measured on the hardware, every accent against every vowel, twice, once
with CAPS on and once with it off:

| | Á | É | Í | Ó | Ú | À | Ã | Õ | Â | Ê | Ô | Ü | Ç |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| upper | 84 | 90 | 89 | 8A | 8B | 8F | B0 | B4 | 8C | 8D | 8E | 9A | 80 |
| lower | A0 | 82 | A1 | A2 | A3 | 85 | B1 | B5 | 83 | 88 | 93 | 81 | 87 |

They had to be measured. The **lower** row is the standard MSX
international character set, but the **upper** row is not: the Hotbit put
its capital accented letters over codes the standard set uses for
something else entirely. 0x84 is A-acute here and a-diaeresis in the
published table. Reading from that table would have produced confident
nonsense, and did, briefly.

Combinations this machine simply does not have, because Portuguese does
not need them: grave on anything but a and u, diaeresis on anything but
u, tilde on n, circumflex on i, and capitals of à-u and û. Those fall
back to the accent as a literal character.

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

## When a keyboard connects but types nothing

This happened, and it is worth knowing because nothing anywhere reports a
fault: the connection is up, the subscription succeeded, and the machine
is simply never told about a keypress.

The cause was Protocol Mode. A BLE HID device can run in Boot Protocol,
where it notifies the Boot Keyboard Input Report (0x2A22), or in Report
Protocol, where everything goes out on the generic Report characteristic
(0x2A4D). A host asks for boot protocol by writing 0 to Protocol Mode
(0x2A4E) - but that characteristic is optional and often not writable,
and the device is free to stay where it is. The keyboard tested here has
no writable Protocol Mode at all, exposes 0x2A22, and never notifies on
it.

So this firmware subscribes to **both**, always. It costs nothing: the
notification callback keeps whatever is shaped like a keyboard report and
ignores the rest. That keyboard turns out to send perfectly ordinary
8-byte boot-shaped reports, just on 0x2A4D, four of which it exposes.

The instruments, on the serial console:

```
h        says whether a keyboard is connected and how many HID
         notifications have arrived - if that number never moves,
         nothing is being sent to us at all
k 1      dump every HID report as hex, with the characteristic it
         came from; k 0 turns it off
```

The connect log also says what was subscribed and whether boot protocol
was accepted, which is where the answer was.

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

## The modifier keys

They are worth more than their labels suggest, because a modifier is a bit
in a byte of its own rather than one of the six key slots, so it arrives
whatever else is being held. See "rollover" below for why that matters.

| modifier | MSX |
|---|---|
| Left Ctrl | CTRL |
| **Left GUI** (Win / Command) | **SPACE - the fire button** |
| **Right Alt** (AltGr) | **STOP**, so Ctrl+AltGr is Break |
| Left Alt (Option) | SELECT |
| Right Ctrl | SPACE as well |
| Right GUI | CODE |

Space is still Space and PageUp and PageDown are still SELECT and STOP.
These are the copies that cannot be lost.

**Fire lives on a modifier because on the keyboard this was built with it
cannot live anywhere else.** Holding two cursor keys for a diagonal and
then Space, that keyboard reports "down and right", or "right and space",
and never all three - it does not even send ErrorRollOver to say so. The
third key is simply not there.

A small keyboard has two keys left of the space bar and they are usually
Left GUI and AltGr, which is why those two carry the keys a game needs
while it is moving. Which physical key sends which depends on whether the
keyboard is in its Mac or its Windows mode, so press both and find out.

**AltGr no longer types accents.** It used to be a second way to reach
them - AltGr+a for a-acute, AltGr+c for c-cedilla - and every one of those
letters is still a dead key away, which is the normal way to type them
here anyway. Nothing on the MSX itself wanted AltGr: its own special keys
are GRAPH and CODE, and GRAPH is on Right GUI.

### GRAPH and CODE

**GRAPH is End.** That block above the cursor keys is where the MSX keys a
PC has no cap for live - PageUp is SELECT, PageDown is STOP - and End was
the only one up there not already spoken for, because it was a second STOP
and PageDown already was one. Home, Insert and Delete are all MSX keys in
their own right and keep their caps.

**CODE is Right GUI**, which a small keyboard does not have, so on one of
those CODE is out of reach. It is the one of the two worth least here: on
a Brazilian machine it reaches the accented letters, and those are already
a dead key away.

### Telling the keyboard and the firmware apart

"This key does nothing" has two causes that look identical from outside:
the keyboard never sent it, or the firmware dropped it. Two console
commands settle it between them.

`j` hands the machine a report nobody typed, so a mapping can be tested
without the key that is meant to produce it:

```
j 5            hold Left Ctrl + Left Alt, which is the MSX's Ctrl+STOP
j 4e           hold PageDown
j 5 4e         both at once
```

With `10 GOTO 10` running in BASIC, `j 5` prints "Parei em 10" - which is
how the Left Alt mapping above was verified without a keyboard that sends
it.

To see what your own keyboard sends, turn the dump on and press each key:

```
k 1
```

Modifiers are printed by name.

## Two keys and a third: rollover

A game that moves diagonally and shoots needs three keys at once, and
plenty of keyboards cannot report three. The six key slots in a USB or
BLE report are filled from a matrix scan, and a cheap matrix cannot
always tell which three of its crossings are closed - so the third key
simply does not arrive, or all six slots come back as `01`, which is
ErrorRollOver: the keyboard saying it does not know.

Nothing in the firmware can recover a key the keyboard never sent. What
it can do is offer a key that does not go through those slots:

**Right Ctrl is a second Space**, which is to say a second fire button.
Modifiers are bits in a byte of their own, on their own matrix line, and
they arrive whatever else is held down. Space still works as Space; this
is the one that cannot be lost. Left Ctrl is still Ctrl, so Ctrl+PageDown
still breaks a running program.

To see which it is on your own keyboard, turn the HID dump on and hold
the combination:

```
k 1
```

Each report is printed raw, and a report carrying `01` in a key slot is
named as one the keyboard cannot resolve. If the three usages are all
there and the game still does not fire, the fault is in this firmware and
not in the keyboard.
