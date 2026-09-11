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
    unsigned char shift;   /* whether it needs Shift                     */
    char          literal; /* the accent as a character in its own right,
                            * 0 when this machine has no key for it      */
};

/* accent -> which dead-key position, and whether it needs Shift. */
static const struct DeadKey kDead[] = {
    /* ACC_NONE       */ {0, 0, 0},
    /* ACC_ACUTE      */ {IDX_ACUTE_GRAVE, 0, '\''},
    /* ACC_GRAVE      */ {IDX_ACUTE_GRAVE, 1, 0},
    /* ACC_TILDE      */ {IDX_TILDE_CIRC,  0, 0},
    /* ACC_CIRCUMFLEX */ {IDX_TILDE_CIRC,  1, '^'},
    /* ACC_DIAERESIS  */ {IDX_DIAERESIS,   0, '"'},
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
    [0x4D] = K_STOP,  /* End -> STOP, the usual emulator convention */
    [0x4B] = K_SELECT,/* PageUp -> SELECT */
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

/* Letters an accent may legally sit on. Anything else means the user
 * typed the dead key as a literal, exactly like on a PC. */
static int composes(unsigned char accent, char c) {
    char l = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    switch (accent) {
        /* Only the combinations this BIOS's character set actually has.
         * Anything else has to fall through to "dead key as a literal",
         * or the accent would silently vanish - which is what the probe
         * runs showed happening for things like u-grave. */
        case ACC_ACUTE:
            return l == 'a' || l == 'e' || l == 'i' || l == 'o' || l == 'u';
        case ACC_CIRCUMFLEX:
            return l == 'a' || l == 'e' || l == 'o';
        case ACC_TILDE:
            return l == 'a' || l == 'o';
        case ACC_GRAVE:
            return l == 'a';
        case ACC_DIAERESIS:
            return l == 'u';
        default:
            return 0;
    }
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

struct Emission { unsigned char row, bit, shift; };

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
        if (composes(accent, c)) {
            /* The BIOS does the composing: dead key, then the letter -
             * and the letter has to go in UNSHIFTED. Holding Shift with
             * it makes this BIOS drop the accent and print the bare
             * letter, which is how a real Hotbit behaves too: with CAPS
             * on (and it boots with CAPS on) you never hold Shift to get
             * a capital. So the machine's own CAPS state decides the
             * case, exactly as it would under someone's fingers. */
            char base = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
            queuePushIdx(kDead[accent].idx, kDead[accent].shift);
            queueChar(base);
            return;
        }
        /* It does not compose, so the user meant the accent as a
         * character - typing " then C, or ' then Space. Pressing the dead
         * key here would be wrong: this BIOS silently drops a dead key it
         * cannot use, which is exactly how the quotes went missing from
         * PRINT "..." the first time round. Use the machine's own key for
         * the character instead, where it has one: " is Shift+6 here and
         * ' is Shift+the key right of P, neither of them a dead key. */
        if (kDead[accent].literal) queueChar(kDead[accent].literal);
    }
    if (c == ' ' && accent != ACC_NONE) return; /* the accent was the point */
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
        if (e->shift) state[M_ROW(K_SHIFT)] &= (unsigned char)~M_BIT(K_SHIFT);
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

    if (mods & 0x11) state[M_ROW(K_CTRL)]  &= (unsigned char)~M_BIT(K_CTRL);
    if (mods & 0x04) state[M_ROW(K_GRAPH)] &= (unsigned char)~M_BIT(K_GRAPH);
    /* Right Alt is AltGr: it belongs to this layer, not to the MSX. */

    for (i = 0; i < 6; i++) {
        unsigned char hid = keys[i];
        unsigned char acc;
        int isNew;
        char c;

        if (hid == 0 || hid == 1) continue; /* empty slot / rollover error */
        if (hid >= 0x68) continue;

        isNew = 1;
        for (j = 0; j < 6; j++) if (sPrevKeys[j] == hid) isNew = 0;

        /* Control-half keys pass straight through and stay down for as
         * long as they are physically down. This is the path games use,
         * and it costs them no latency at all. */
        if (kHidControl[hid]) {
            unsigned short m = kHidControl[hid];
            if (sPendingAccent != ACC_NONE) {
                /* An accent was waiting and the next key is Space or
                 * Return, so the user meant the accent as a character:
                 * ' then Space is an apostrophe. Both go through the
                 * queue, in that order. */
                if (isNew) {
                    if (kDead[sPendingAccent].literal)
                        queueChar(kDead[sPendingAccent].literal);
                    queuePush(M_ROW(m), M_BIT(m), 0);
                    sPendingAccent = ACC_NONE;
                    markConsumed(hid);
                }
                continue;
            }
            state[M_ROW(m)] &= (unsigned char)~M_BIT(m);
            continue;
        }

        if (isConsumed(hid)) continue;

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
