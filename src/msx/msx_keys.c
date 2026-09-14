/* msx_keys.c - US-International PC keyboard -> Hotbit HB-8000 key matrix.
 *
 * THE PROBLEM THIS SOLVES
 * -----------------------
 * The BLE keyboard is a physical US keyboard and sends raw USB HID
 * position codes. The emulated machine is a Brazilian Sharp/Epcom Hotbit
 * HB-8000, whose keyboard matrix does NOT match the international MSX
 * layout that stock fMSX assumes: on the Hotbit, ';' is Shift+the comma
 * key, ':' is Shift+the period key, '[' and ']' share one key, 'Ç' has a
 * key of its own, and three positions are dead keys for accents.
 *
 * So we ignore fMSX's own Keys[] table (which encodes the international
 * layout) and rebuild the 16-byte matrix image ourselves every frame,
 * from a table of what THIS machine's BIOS actually decodes each matrix
 * position to. That table was read straight out of the BIOS ROM's own
 * key-translation tables rather than guessed - see
 * tools/dump_hotbit_layout.py, which reprints it from a ROM image.
 *
 * WHAT THE USER GETS
 * ------------------
 * The keyboard behaves like US-International: what is printed on the
 * keycap is what appears on the MSX, and the dead keys compose accents
 * (' + a = a-acute, ~ + a = a-tilde, and so on). Where the Hotbit needs a
 * different shift state than the one being physically held, we fake the
 * Shift contact for the duration of that keypress.
 *
 * WHY A FULL REBUILD EVERY FRAME
 * ------------------------------
 * The BIOS scans the matrix once per VDP interrupt, and Keyboard() is
 * called at exactly that point (MSX.c, ScanLine==192). Recomputing the
 * whole matrix from the current HID report each frame is both correct and
 * far less bug-prone than tracking press/release deltas - the earlier
 * version of this file diffed reports and had to special-case left/right
 * modifiers holding the same contact.
 */
#include <string.h>
#include "msx_keys.h"
#include "msx_bridge.h"
#include "selector.h"

/* ---------------------------------------------------------------- */
/* The Hotbit matrix, rows 0-5 (the printable half).                 */
/*                                                                   */
/* Index = row * 8 + bit number. Each entry is what the BIOS prints   */
/* for that position unshifted and shifted. 0 marks the three dead    */
/* keys, which produce no character of their own.                     */
/* ---------------------------------------------------------------- */
#define DEAD 0

static const char kLayout[48][2] = {
    /* row 0 */
    {'0', ')'}, {'1', '!'}, {'2', '@'}, {'3', '#'},
    {'4', '$'}, {'5', '%'}, {'6', '"'}, {'7', '&'},
    /* row 1 */
    {'8', '*'}, {'9', '('}, {'-', '_'}, {'=', '+'},
    {'\\', '^'}, {DEAD, DEAD}, {DEAD, '\''}, {'\x87', '\x80'}, /* c-cedilla / C-cedilla */
    /* row 2 */
    {DEAD, DEAD}, {'[', ']'}, {',', ';'}, {'.', ':'},
    {'/', '?'}, {'<', '>'}, {'a', 'A'}, {'b', 'B'},
    /* row 3 */
    {'c', 'C'}, {'d', 'D'}, {'e', 'E'}, {'f', 'F'},
    {'g', 'G'}, {'h', 'H'}, {'i', 'I'}, {'j', 'J'},
    /* row 4 */
    {'k', 'K'}, {'l', 'L'}, {'m', 'M'}, {'n', 'N'},
    {'o', 'O'}, {'p', 'P'}, {'q', 'Q'}, {'r', 'R'},
    /* row 5 */
    {'s', 'S'}, {'t', 'T'}, {'u', 'U'}, {'v', 'V'},
    {'w', 'W'}, {'x', 'X'}, {'y', 'Y'}, {'z', 'Z'},
};

/* The three dead-key positions. The ROM's tables only mark them 0xFF and
 * do not say which accent each one carries, so this was settled on the
 * hardware itself: the `d` command in the serial console presses a dead
 * key followed by a letter and reads the resulting character back out of
 * the VDP, and the `g` command draws its glyph. What came back:
 *
 *   row 1, 0x20  unshifted -> a-acute      shifted -> a-grave
 *   row 1, 0x40  unshifted -> u-diaeresis  shifted -> apostrophe (not dead)
 *   row 2, 0x01  unshifted -> a-tilde      shifted -> a-circumflex
 *
 * The giveaway was which letters refuse to compose: grave only takes an
 * a, diaeresis only a u, circumflex takes a and e but not u - exactly the
 * set Portuguese uses, which is a stronger signal than any glyph. */
#define IDX_ACUTE_GRAVE  (1 * 8 + 5) /* row 1, bit 0x20 */
#define IDX_DIAERESIS    (1 * 8 + 6) /* row 1, bit 0x40 */
#define IDX_TILDE_CIRC   (2 * 8 + 0) /* row 2, bit 0x01 */

enum { ACC_NONE = 0, ACC_ACUTE, ACC_GRAVE, ACC_TILDE, ACC_CIRCUMFLEX, ACC_DIAERESIS };

struct DeadKey {
    unsigned char idx;     /* matrix position of the dead key            */
    unsigned char shift;   /* modifier mask: bit 0 Shift, bit 1 Ctrl     */
    unsigned char literal; /* the accent as a character in its own right.
                            * Typed through the keyboard buffer, not the
                            * matrix, because this keyboard has no key at
                            * all for ~ or ` even though the character set
                            * has both (0x7E and 0x60, glyphs confirmed on
                            * the machine). */
};

/* accent -> which dead-key position, and whether it needs Shift. */
static const struct DeadKey kDead[] = {
    /* ACC_NONE       */ {0, 0, 0x00},
    /* ACC_ACUTE      */ {IDX_ACUTE_GRAVE, 0, 0x27}, /* ' */
    /* ACC_GRAVE      */ {IDX_ACUTE_GRAVE, 1, 0x60}, /* ` */
    /* ACC_TILDE      */ {IDX_TILDE_CIRC,  0, 0x7E}, /* ~ */
    /* ACC_CIRCUMFLEX */ {IDX_TILDE_CIRC,  1, 0x5E}, /* ^ */
    /* ACC_DIAERESIS  */ {IDX_DIAERESIS,   0, 0x22}, /* " */
};

static const char *kAccentNames[] = {
    "", "acute", "grave", "tilde", "circumflex", "diaeresis"
};

/* ---------------------------------------------------------------- */
/* Matrix rows 6-10: the control half, identical on every MSX.        */
/* ---------------------------------------------------------------- */
#define M(row, bit) (((row) << 8) | (bit))
#define M_ROW(m) ((m) >> 8)
#define M_BIT(m) ((m) & 0xFF)

#define K_SHIFT   M(6, 0x01)
#define K_CTRL    M(6, 0x02)
#define K_GRAPH   M(6, 0x04)
#define K_CAPS    M(6, 0x08)
#define K_CODE    M(6, 0x10)
#define K_F1      M(6, 0x20)
#define K_F2      M(6, 0x40)
#define K_F3      M(6, 0x80)
#define K_F4      M(7, 0x01)
#define K_F5      M(7, 0x02)
#define K_ESC     M(7, 0x04)
#define K_TAB     M(7, 0x08)
#define K_STOP    M(7, 0x10)
#define K_BS      M(7, 0x20)
#define K_SELECT  M(7, 0x40)
#define K_RETURN  M(7, 0x80)
#define K_SPACE   M(8, 0x01)
#define K_HOME    M(8, 0x02)
#define K_INS     M(8, 0x04)
#define K_DEL     M(8, 0x08)
#define K_LEFT    M(8, 0x10)
#define K_UP      M(8, 0x20)
#define K_DOWN    M(8, 0x40)
#define K_RIGHT   M(8, 0x80)

/* HID usage code -> a control-half matrix position (0 = not one). */
static const unsigned short kHidControl[0x68] = {
    [0x28] = K_RETURN, [0x29] = K_ESC,  [0x2A] = K_BS,   [0x2B] = K_TAB,
    [0x2C] = K_SPACE,  [0x39] = K_CAPS,
    [0x3A] = K_F1, [0x3B] = K_F2, [0x3C] = K_F3, [0x3D] = K_F4, [0x3E] = K_F5,
    [0x49] = K_INS, [0x4A] = K_HOME, [0x4C] = K_DEL,
    /* The MSX keys a PC keyboard has no cap for go on the page keys:
     * PageDown is STOP, so Ctrl+PageDown is BREAK and is the way to
     * interrupt a running BASIC program, and PageUp is SELECT. End is
     * STOP as well, which costs nothing and is the usual emulator
     * convention. */
    [0x4E] = K_STOP,
    [0x4D] = K_STOP,
    [0x4B] = K_SELECT,
    [0x4F] = K_RIGHT, [0x50] = K_LEFT, [0x51] = K_DOWN, [0x52] = K_UP,
    [0x58] = K_RETURN,
};

/* ---------------------------------------------------------------- */
/* The US-International layer: HID code -> character, or dead key.    */
/* ---------------------------------------------------------------- */
static const char kUsBase[0x68] = {
    [0x04] = 'a', [0x05] = 'b', [0x06] = 'c', [0x07] = 'd', [0x08] = 'e',
    [0x09] = 'f', [0x0A] = 'g', [0x0B] = 'h', [0x0C] = 'i', [0x0D] = 'j',
    [0x0E] = 'k', [0x0F] = 'l', [0x10] = 'm', [0x11] = 'n', [0x12] = 'o',
    [0x13] = 'p', [0x14] = 'q', [0x15] = 'r', [0x16] = 's', [0x17] = 't',
    [0x18] = 'u', [0x19] = 'v', [0x1A] = 'w', [0x1B] = 'x', [0x1C] = 'y',
    [0x1D] = 'z',
    [0x1E] = '1', [0x1F] = '2', [0x20] = '3', [0x21] = '4', [0x22] = '5',
    [0x23] = '6', [0x24] = '7', [0x25] = '8', [0x26] = '9', [0x27] = '0',
    [0x2D] = '-', [0x2E] = '=', [0x2F] = '[', [0x30] = ']', [0x31] = '\\',
    [0x33] = ';', [0x34] = '\'', [0x35] = '`',
    [0x36] = ',', [0x37] = '.', [0x38] = '/',
    /* keypad */
    [0x54] = '/', [0x55] = '*', [0x56] = '-', [0x57] = '+',
    [0x59] = '1', [0x5A] = '2', [0x5B] = '3', [0x5C] = '4', [0x5D] = '5',
    [0x5E] = '6', [0x5F] = '7', [0x60] = '8', [0x61] = '9', [0x62] = '0',
    [0x63] = '.',
};

static const char kUsShift[0x68] = {
    [0x04] = 'A', [0x05] = 'B', [0x06] = 'C', [0x07] = 'D', [0x08] = 'E',
    [0x09] = 'F', [0x0A] = 'G', [0x0B] = 'H', [0x0C] = 'I', [0x0D] = 'J',
    [0x0E] = 'K', [0x0F] = 'L', [0x10] = 'M', [0x11] = 'N', [0x12] = 'O',
    [0x13] = 'P', [0x14] = 'Q', [0x15] = 'R', [0x16] = 'S', [0x17] = 'T',
    [0x18] = 'U', [0x19] = 'V', [0x1A] = 'W', [0x1B] = 'X', [0x1C] = 'Y',
    [0x1D] = 'Z',
    [0x1E] = '!', [0x1F] = '@', [0x20] = '#', [0x21] = '$', [0x22] = '%',
    [0x23] = 0,   /* Shift+6 is the circumflex dead key on US-International */
    [0x24] = '&', [0x25] = '*', [0x26] = '(', [0x27] = ')',
    [0x2D] = '_', [0x2E] = '+', [0x2F] = '{', [0x30] = '}', [0x31] = '|',
    [0x33] = ':', [0x34] = 0, /* Shift+' is the diaeresis dead key */
    [0x35] = 0,               /* Shift+` is the tilde dead key     */
    [0x36] = '<', [0x37] = '>', [0x38] = '?',
};

/* Which HID code + shift state is a dead key rather than a character. */
static unsigned char deadKeyFor(unsigned char hid, int shift) {
    if (hid == 0x34) return shift ? ACC_DIAERESIS : ACC_ACUTE;   /* ' and " */
    if (hid == 0x35) return shift ? ACC_TILDE     : ACC_GRAVE;   /* ` and ~ */
    if (hid == 0x23 && shift) return ACC_CIRCUMFLEX;             /* Shift+6 */
    return ACC_NONE;
}

/* AltGr shortcuts a US-International user expects, for the characters
 * worth reaching without the dead-key dance. Only c-cedilla has a key of
 * its own on the Hotbit; the rest go through the dead keys like normal. */
static unsigned char altGrAccent(unsigned char hid) {
    switch (hid) {
        case 0x04: case 0x08: case 0x0C: case 0x12: case 0x18: /* a e i o u */
            return ACC_ACUTE;
        case 0x11: return ACC_TILDE; /* n */
        default:   return ACC_NONE;
    }
}

/* Accent + letter -> the character code this machine actually has.
 *
 * Measured on the hardware, one pair at a time, because the Hotbit's
 * character set is NOT the standard MSX international one. Acute+A is
 * 0x84 here; in the international set 0x84 is a-diaeresis. Guessing from
 * the published MSX table would have produced confident nonsense.
 *
 * A zero means this machine has no such character, and there are plenty:
 * grave only exists on an A, diaeresis only on a U, and neither acute+C
 * nor tilde+N compose at all. That is not a gap, it is a Brazilian
 * keyboard - c-cedilla has a key of its own here rather than being built
 * from a dead key, which is why `'c` has to be special-cased to it.
 *
 * Filled in by tools/measure_charset.py; see docs/KEYBOARD.md. */
struct Composed {
    unsigned char accent;
    char          base;   /* always lower case */
    unsigned char upper;  /* code when the machine would print a capital */
    unsigned char lower;
};

static const struct Composed kComposed[] = {
    /*  accent          base  UPPER  lower */
    { ACC_ACUTE,      'a',  0x84,  0xA0 },
    { ACC_ACUTE,      'e',  0x90,  0x82 },
    { ACC_ACUTE,      'i',  0x89,  0xA1 },
    { ACC_ACUTE,      'o',  0x8A,  0xA2 },
    { ACC_ACUTE,      'u',  0x8B,  0xA3 },
    { ACC_GRAVE,      'a',  0x8F,  0x85 },
    { ACC_GRAVE,      'u',  0x00,  0x97 },
    { ACC_TILDE,      'a',  0xB0,  0xB1 },
    { ACC_TILDE,      'o',  0xB4,  0xB5 },
    { ACC_CIRCUMFLEX, 'a',  0x8C,  0x83 },
    { ACC_CIRCUMFLEX, 'e',  0x8D,  0x88 },
    { ACC_CIRCUMFLEX, 'o',  0x8E,  0x93 },
    { ACC_CIRCUMFLEX, 'u',  0x00,  0x96 },
    { ACC_DIAERESIS,  'u',  0x9A,  0x81 },
    /* c-cedilla is not composed on this machine - acute+c produces a
     * plain c. It has a key of its own instead, and these are the codes
     * that key produces, from the BIOS's own table. US-International
     * users type it as ' then c, so that is what this entry is for. */
    { ACC_ACUTE,      'c',  0x80,  0x87 },
    { 0, 0, 0, 0 }
};

/* Worth knowing when reading the table above: the LOWER column is the
 * standard MSX international character set (a-acute at 0xA0, e-acute at
 * 0x82 and so on), but the UPPER column is not - the Hotbit put its
 * capital accented letters over glyphs that the standard set uses for
 * something else entirely. 0x84 is A-acute here and a-diaeresis in the
 * published table. Every value above was read off the machine rather than
 * from that table, which is the only reason they are right. */

/* Returns the character code for `accent` over `base`, or 0 if this
 * machine has no such character. */
static unsigned char composedCode(unsigned char accent, char base, int upper) {
    char l = (base >= 'A' && base <= 'Z') ? (char)(base + 32) : base;
    int i;
    for (i = 0; kComposed[i].base; i++) {
        if (kComposed[i].accent != accent || kComposed[i].base != l) continue;
        return upper ? kComposed[i].upper : kComposed[i].lower;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* Reverse lookup: character -> matrix position + shift.              */
/* Built once at startup from kLayout, so the layout is written down  */
/* exactly once.                                                      */
/* ---------------------------------------------------------------- */
static signed char sCharIdx[128];
static unsigned char sCharShift[128];

static void buildUsReverse(void); /* defined with the typing queue below */

void msx_keys_init(void) {
    int i, s;
    memset(sCharIdx, -1, sizeof(sCharIdx));
    memset(sCharShift, 0, sizeof(sCharShift));
    for (i = 0; i < 48; i++) {
        for (s = 0; s < 2; s++) {
            unsigned char c = (unsigned char)kLayout[i][s];
            if (c == DEAD || c >= 128) continue;
            if (sCharIdx[c] >= 0) continue; /* first position wins */
            sCharIdx[c] = (signed char)i;
            sCharShift[c] = (unsigned char)s;
        }
    }
    buildUsReverse();
}

/* ---------------------------------------------------------------- */
/* Emission queue, for anything that needs more than one keypress     */
/* (an accent is the dead key followed by the letter). The BIOS only  */
/* looks at the matrix once per frame, so each emitted press is held  */
/* for a few frames and then released for a few.                      */
/* ---------------------------------------------------------------- */
#define HOLD_FRAMES 3
#define GAP_FRAMES  2
#define QUEUE_LEN   8

struct Emission { unsigned char row, bit, shift; }; /* shift: bit0 Shift, bit1 Ctrl */

static struct Emission sQueue[QUEUE_LEN];
static unsigned char sQHead, sQTail;
static unsigned char sQPhase;   /* frames left in the current hold or gap */
static unsigned char sQHolding; /* 1 while the current emission is pressed */

static void queuePush(unsigned char row, unsigned char bit, unsigned char shift) {
    unsigned char next = (unsigned char)((sQTail + 1) % QUEUE_LEN);
    if (next == sQHead) return; /* full: dropping beats blocking the frame */
    sQueue[sQTail].row = row;
    sQueue[sQTail].bit = bit;
    sQueue[sQTail].shift = shift;
    sQTail = next;
}

static void queuePushIdx(unsigned char idx, unsigned char shift) {
    queuePush((unsigned char)(idx / 8), (unsigned char)(1 << (idx % 8)), shift);
}

static int queueEmpty(void) { return sQHead == sQTail; }

/* ---------------------------------------------------------------- */
/* Report state                                                       */
/* ---------------------------------------------------------------- */
static volatile unsigned char sReport[8];
static unsigned char sPrevKeys[6];
static unsigned char sConsumed[6];   /* HID codes already dealt with on their
                                      * down edge; they must not also be held
                                      * down in the matrix. Cleared when the
                                      * key is physically released. */
static unsigned char sPendingAccent;

void msx_keys_set_report(const uint8_t report[8]) {
    int i, j;

    /* F12 is not an MSX key, so it is free to open the selector. Acted on
     * at its down edge only. */
    for (i = 2; i < 8; i++) {
        if (report[i] != 0x45) continue;
        for (j = 2; j < 8; j++) if (sReport[j] == 0x45) break;
        if (j >= 8) selector_open();
        break;
    }

    memcpy((void *)sReport, report, 8);
}

/* ---------------------------------------------------------------- */
/* Synthetic typing, for the serial self-test.                        */
/*                                                                    */
/* These are fed in as HID reports, not as matrix positions, so a      */
/* typed string goes through the very same US-International layer,     */
/* dead keys and matrix rebuild a real keyboard would drive. Typing     */
/* "'a" therefore composes an a-acute, exactly as it would under the    */
/* user's fingers.                                                     */
/* ---------------------------------------------------------------- */
#define TYPE_LEN   96
#define TYPE_HOLD  4
#define TYPE_GAP   3

static unsigned char sTypeHid[TYPE_LEN], sTypeMods[TYPE_LEN];
static unsigned char sTHead, sTTail, sTPhase, sTDown;

/* character -> the US-International keystroke that produces it */
static unsigned char sHidForChar[128];
static unsigned char sShiftForChar[128];

static void buildUsReverse(void) {
    int h;
    memset(sHidForChar, 0, sizeof(sHidForChar));
    memset(sShiftForChar, 0, sizeof(sShiftForChar));
    for (h = 0; h < 0x68; h++) {
        unsigned char c = (unsigned char)kUsBase[h];
        if (c && c < 128 && !sHidForChar[c]) { sHidForChar[c] = (unsigned char)h; }
    }
    for (h = 0; h < 0x68; h++) {
        unsigned char c = (unsigned char)kUsShift[h];
        if (c && c < 128 && !sHidForChar[c]) {
            sHidForChar[c] = (unsigned char)h;
            sShiftForChar[c] = 1;
        }
    }
    /* Space and Enter are control-half keys, not characters. */
    sHidForChar[' '] = 0x2C;
    sHidForChar['\n'] = 0x28;

    /* The dead keys produce no character themselves, so the loops above
     * left them out - but they are still the keystroke a US-International
     * user presses to get these five characters, and the composition
     * logic turns them back into the right thing. Without these, typing
     * a double quote through the serial console silently does nothing,
     * which is not a keyboard bug but a hole in the test harness. */
    sHidForChar['\''] = 0x34;
    sHidForChar['"']  = 0x34; sShiftForChar['"'] = 1;
    sHidForChar['`']  = 0x35;
    sHidForChar['~']  = 0x35; sShiftForChar['~'] = 1;
    sHidForChar['^']  = 0x23; sShiftForChar['^'] = 1;
}

int msx_keys_type(const char *text) {
    int n = 0;
    for (; *text; text++) {
        unsigned char c = (unsigned char)*text;
        unsigned char next;
        if (c >= 128 || !sHidForChar[c]) continue;
        next = (unsigned char)((sTTail + 1) % TYPE_LEN);
        if (next == sTHead) break;
        sTypeHid[sTTail] = sHidForChar[c];
        sTypeMods[sTTail] = sShiftForChar[c] ? 0x02 : 0x00; /* left Shift */
        sTTail = next;
        n++;
    }
    return n;
}

int msx_keys_typing(void) { return sTHead != sTTail || sTDown; }

/* Fill `report` from the typing queue. `frozen` holds the timer still
 * while a multi-key emission (an accent) is still going into the matrix.
 * Returns 1 if the queue is driving the keyboard this frame. */
static int typingFillReport(unsigned char *report, int frozen) {
    memset(report, 0, 8);
    if (sTHead == sTTail && !sTDown) return 0;

    if (!frozen && sTPhase) sTPhase--;
    if (!frozen && !sTPhase) {
        if (sTDown) {
            sTDown = 0;
            sTPhase = TYPE_GAP;
            sTHead = (unsigned char)((sTHead + 1) % TYPE_LEN);
        } else if (sTHead != sTTail) {
            sTDown = 1;
            sTPhase = TYPE_HOLD;
        }
    }
    if (sTDown && sTHead != sTTail) {
        report[0] = sTypeMods[sTHead];
        report[2] = sTypeHid[sTHead];
    }
    return 1;
}

void msx_keys_press_matrix(int row, int bit, int mods) {
    if (row < 0 || row > 15 || bit <= 0 || bit > 0xFF) return;
    queuePush((unsigned char)row, (unsigned char)bit, (unsigned char)(mods & 3));
}

void msx_keys_probe_dead(int which, int shift, char base) {
    static const unsigned char idx[3] = { IDX_ACUTE_GRAVE, IDX_DIAERESIS, IDX_TILDE_CIRC };
    if (which < 0 || which > 2) return;
    queuePushIdx(idx[which], (unsigned char)(shift ? 1 : 0));
    if (base && (unsigned char)base < 128 && sCharIdx[(unsigned char)base] >= 0)
        queuePushIdx((unsigned char)sCharIdx[(unsigned char)base],
                     sCharShift[(unsigned char)base]);
}

const char *msx_keys_pending_accent(void) {
    return kAccentNames[sPendingAccent];
}

static int isConsumed(unsigned char hid) {
    int i;
    for (i = 0; i < 6; i++) if (sConsumed[i] == hid) return 1;
    return 0;
}

static void markConsumed(unsigned char hid) {
    int i;
    for (i = 0; i < 6; i++) if (sConsumed[i] == 0) { sConsumed[i] = hid; return; }
}

/* Queue the keypresses that produce `c` behind the accent waiting in
 * front of it (ACC_NONE for a plain character). */
/* Queue the matrix position that produces character `c`, if this machine
 * has one. */
static void queueChar(char c) {
    if ((unsigned char)c < 128 && sCharIdx[(unsigned char)c] >= 0)
        queuePushIdx((unsigned char)sCharIdx[(unsigned char)c],
                     sCharShift[(unsigned char)c]);
}

/* Queue the keypresses that produce `c` behind the accent waiting in
 * front of it (ACC_NONE for a plain character). */
static void emitChar(unsigned char accent, char c) {
    if (accent != ACC_NONE) {
        /* The case of an accented letter is ours to decide, which is the
         * whole reason this goes through the keyboard buffer rather than
         * through the dead keys. Driving the BIOS's own composition meant
         * the case followed CAPS and Shift did nothing at all, because
         * this BIOS drops the accent outright when Shift is held with the
         * letter - so a capital accented letter meant reaching for CAPS.
         *
         * Case here is worked out the way this machine works it out for an
         * ordinary letter, which was measured rather than assumed: with
         * CAPS on, a letter key gives a capital whether or not Shift is
         * held. So the rule is CAPS OR Shift, not the exclusive-or a PC
         * would use. Accented letters follow the plain ones; being
         * consistent with the machine matters more than being able to
         * reach every case from every state. */
        int shifted = (c >= 'A' && c <= 'Z');
        int upper = msx_caps_on() || shifted;
        unsigned char code = composedCode(accent, c, upper);
        if (code) {
            msx_type_char(code);
            return;
        }
        /* No such character on this machine, so the user meant the accent
         * as a character: `"` then `S`, or `'` then Space. Type the accent
         * itself where there is one, then carry on with what they pressed.
         * Pressing the dead key here would be wrong - this BIOS silently
         * swallows a dead key it cannot use, which is how the quotes went
         * missing from PRINT "..." the first time round. */
        if (kDead[accent].literal) msx_type_char(kDead[accent].literal);
        /* A space after a dead key is part of the sequence, not a space to
         * type: `'` then Space is an apostrophe and nothing else. */
        if (c == ' ') return;
    }
    queueChar(c);
}

/* Advance the queue by one frame and stamp the current emission into the
 * matrix image. Returns 1 while the queue owns the matrix. */
static int queueFrame(unsigned char *state) {
    if (queueEmpty() && !sQHolding) return 0;

    if (sQPhase) sQPhase--;
    if (!sQPhase) {
        if (sQHolding) {
            /* End of a hold: release, and step to the next emission. */
            sQHolding = 0;
            sQPhase = GAP_FRAMES;
            sQHead = (unsigned char)((sQHead + 1) % QUEUE_LEN);
        } else if (!queueEmpty()) {
            sQHolding = 1;
            sQPhase = HOLD_FRAMES;
        }
    }

    if (sQHolding && !queueEmpty()) {
        const struct Emission *e = &sQueue[sQHead];
        state[e->row] &= (unsigned char)~e->bit;
        if (e->shift & 1) state[M_ROW(K_SHIFT)] &= (unsigned char)~M_BIT(K_SHIFT);
        if (e->shift & 2) state[M_ROW(K_CTRL)]  &= (unsigned char)~M_BIT(K_CTRL);
    }
    return 1;
}

void msx_keys_frame(void) {
    unsigned char state[16];
    unsigned char report[8];
    unsigned char mods;
    const unsigned char *keys;
    int i, j;
    int physShift, forceShift, forceNoShift;

    memset(state, 0xFF, sizeof(state)); /* contact open = key not pressed */

    if (!typingFillReport(report, !queueEmpty() || sQHolding))
        memcpy(report, (const void *)sReport, 8);
    mods = report[0];
    keys = &report[2];

    /* Drop consumed marks for keys that have since been released, so the
     * same physical key can be pressed again. */
    for (i = 0; i < 6; i++) {
        int stillDown = 0;
        if (!sConsumed[i]) continue;
        for (j = 0; j < 6; j++) if (keys[j] == sConsumed[i]) stillDown = 1;
        if (!stillDown) sConsumed[i] = 0;
    }

    /* An emission in flight owns the matrix until it drains: nobody types
     * an accent and plays a game in the same 80 milliseconds. */
    if (queueFrame(state)) {
        memcpy(sPrevKeys, keys, 6);
        msx_kbd_write(state);
        return;
    }

    physShift = (mods & 0x22) ? 1 : 0; /* either Shift */
    forceShift = forceNoShift = 0;

    if (mods & 0x01) state[M_ROW(K_CTRL)]  &= (unsigned char)~M_BIT(K_CTRL);
    /* Right Alt is AltGr: it belongs to this layer, not to the MSX. */

    /* Right Ctrl is a second Space, which is to say a second fire button.
     *
     * This is not a preference, it is a way round a limit in the keyboard
     * rather than in either machine. A USB or BLE keyboard reports up to
     * six keys in six slots, and a cheap matrix cannot always resolve
     * which three of them are down: hold two cursor keys for a diagonal
     * and the third key often does not arrive at all, which in a game
     * means the ship moves but will not shoot. Modifiers do not live in
     * those slots. They are bits in a byte of their own, on their own
     * matrix line, and they arrive whatever else is held.
     *
     * Space still works as Space. This is simply one that cannot be lost.
     * Left Ctrl is still Ctrl, so Ctrl+PageDown still breaks a program. */
    if (mods & 0x10) state[M_ROW(K_SPACE)] &= (unsigned char)~M_BIT(K_SPACE);

    /* The two keys left of the space bar, for SELECT and STOP.
     *
     * A small keyboard has no function keys and often no right Ctrl, but
     * it always has these two and they sit under a thumb. They send Left
     * GUI and Left Alt - which of them sends which depends on whether the
     * keyboard is in its Mac or its Windows mode, so try both and keep
     * the one you like. Being modifiers, they also survive being held
     * with two cursor keys, which is the whole reason for putting them
     * here rather than on PageUp and PageDown. Those still work too.
     *
     * Left Alt was the MSX's GRAPH key and GRAPH moves to Right GUI,
     * which a fuller keyboard has and a small one does not. That is a
     * real loss on a small keyboard, and it is the right way round: STOP
     * under a thumb is worth more than a modifier for typing graphic
     * characters. */
    if (mods & 0x08) state[M_ROW(K_SELECT)] &= (unsigned char)~M_BIT(K_SELECT);
    if (mods & 0x04) state[M_ROW(K_STOP)]   &= (unsigned char)~M_BIT(K_STOP);
    if (mods & 0x80) state[M_ROW(K_GRAPH)]  &= (unsigned char)~M_BIT(K_GRAPH);

    for (i = 0; i < 6; i++) {
        unsigned char hid = keys[i];
        unsigned char acc;
        int isNew;
        char c;

        if (hid == 0 || hid == 1) continue; /* empty slot / rollover error */
        if (hid >= 0x68) continue;

        isNew = 1;
        for (j = 0; j < 6; j++) if (sPrevKeys[j] == hid) isNew = 0;

        /* Anything already dealt with on its down edge stays dealt with
         * until it is released. This check has to come BEFORE the
         * control-key path: a Space that was swallowed to finish a dead
         * key would otherwise be pressed again on the very next frame,
         * which is where the stray space after an accent came from. */
        if (isConsumed(hid)) continue;

        /* Control-half keys pass straight through and stay down for as
         * long as they are physically down. This is the path games use,
         * and it costs them no latency at all. */
        if (kHidControl[hid]) {
            unsigned short m = kHidControl[hid];
            if (sPendingAccent != ACC_NONE) {
                /* An accent was waiting and a control key arrived, so the
                 * user meant the accent as a character of its own. */
                if (isNew) {
                    if (kDead[sPendingAccent].literal)
                        msx_type_char(kDead[sPendingAccent].literal);
                    sPendingAccent = ACC_NONE;
                    markConsumed(hid);
                    /* Space is SWALLOWED: on a US-International keyboard
                     * `'` then Space is an apostrophe and nothing else,
                     * not an apostrophe and a space. Anything else - a
                     * Return, an arrow - still does its own job. */
                    if (m != K_SPACE) {
                        state[M_ROW(m)] &= (unsigned char)~M_BIT(m);
                        continue;
                    }
                }
                continue;
            }
            state[M_ROW(m)] &= (unsigned char)~M_BIT(m);
            continue;
        }

        /* A dead key produces nothing on its own; it colours the next
         * character instead. */
        acc = deadKeyFor(hid, physShift);
        if (acc != ACC_NONE) {
            if (isNew) {
                if (sPendingAccent != ACC_NONE) {
                    /* Two dead keys running: the first becomes literal,
                     * exactly like on a PC. */
                    emitChar(sPendingAccent, ' ');
                }
                sPendingAccent = acc;
                markConsumed(hid);
            }
            continue;
        }

        c = physShift ? kUsShift[hid] : kUsBase[hid];
        if (!c) continue;

        /* AltGr+c is c-cedilla, which has a key of its own here. */
        if ((mods & 0x40) && hid == 0x06) {
            if (isNew) {
                queuePushIdx((unsigned char)(1 * 8 + 7), (unsigned char)physShift);
                markConsumed(hid);
            }
            continue;
        }
        /* The other AltGr shortcuts go through the dead keys. */
        if ((mods & 0x40) && altGrAccent(hid) != ACC_NONE) {
            if (isNew) {
                emitChar(altGrAccent(hid), c);
                markConsumed(hid);
            }
            continue;
        }
        if (sPendingAccent != ACC_NONE) {
            if (isNew) {
                emitChar(sPendingAccent, c);
                sPendingAccent = ACC_NONE;
                markConsumed(hid);
            }
            continue;
        }

        /* Plain character: hold its matrix position for as long as the
         * key is held, faking Shift when this machine wants a different
         * shift state than the user has down (its comma key is ';' when
         * shifted, for instance, where a US keyboard has ';' unshifted). */
        if ((unsigned char)c < 128 && sCharIdx[(unsigned char)c] >= 0) {
            unsigned char idx = (unsigned char)sCharIdx[(unsigned char)c];
            state[idx / 8] &= (unsigned char)~(1 << (idx % 8));
            if (sCharShift[(unsigned char)c]) forceShift = 1;
            else forceNoShift = 1;
        }
    }

    if (forceShift || (physShift && !forceNoShift))
        state[M_ROW(K_SHIFT)] &= (unsigned char)~M_BIT(K_SHIFT);

    memcpy(sPrevKeys, keys, 6);
    msx_kbd_write(state);
}
