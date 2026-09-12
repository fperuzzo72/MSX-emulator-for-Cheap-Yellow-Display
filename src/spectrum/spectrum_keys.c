/* spectrum_keys.c - a USB keyboard onto the Spectrum's 8x5 matrix.
 *
 * The Spectrum reads its keyboard through IN from port 0xFE, with the
 * high address byte selecting which half-rows to read: a zero bit there
 * means "include this half-row", and each returns five bits, low for
 * pressed. So the matrix is eight bytes and the read is an AND of the
 * selected ones.
 *
 * A PC keyboard has keys the Spectrum does not, and the Spectrum has
 * meanings a PC reaches differently. Caps Shift is Shift, Symbol Shift is
 * Ctrl, and the things a Spectrum expresses as Caps Shift combinations -
 * the arrows, Delete - are synthesised here so the PC keys do what is
 * printed on them.
 */
#include <string.h>
#include "spectrum.h"

/* Half-rows, in the order the address lines select them. */
enum {
    ROW_CAPS_V = 0,  /* CAPS SHIFT Z X C V */
    ROW_A_G,         /* A S D F G          */
    ROW_Q_T,         /* Q W E R T          */
    ROW_1_5,         /* 1 2 3 4 5          */
    ROW_0_6,         /* 0 9 8 7 6          */
    ROW_P_Y,         /* P O I U Y          */
    ROW_ENTER_H,     /* ENTER L K J H      */
    ROW_SPACE_B      /* SPACE SYMSHIFT M N B */
};

/* Bit 0 is the leftmost key of each half-row above. */
static uint8_t sMatrix[8];

#define KEY(row, bit) (uint8_t)(((row) << 3) | (bit))
#define KEY_ROW(k) ((k) >> 3)
#define KEY_BIT(k) ((k) & 7)
#define NO_KEY 0xFF

#define K_CAPS   KEY(ROW_CAPS_V, 0)
#define K_SYM    KEY(ROW_SPACE_B, 1)
#define K_ENTER  KEY(ROW_ENTER_H, 0)
#define K_SPACE  KEY(ROW_SPACE_B, 0)

/* HID usage code -> Spectrum key, and whether it needs a shift. */
struct HidKey { uint8_t key, caps, sym; };

static const struct HidKey kHid[0x68] = {
    /* letters, in HID order a..z */
    [0x04] = {KEY(ROW_A_G,0),0,0},      [0x05] = {KEY(ROW_SPACE_B,4),0,0},
    [0x06] = {KEY(ROW_CAPS_V,3),0,0},   [0x07] = {KEY(ROW_A_G,2),0,0},
    [0x08] = {KEY(ROW_Q_T,2),0,0},      [0x09] = {KEY(ROW_A_G,3),0,0},
    [0x0A] = {KEY(ROW_A_G,4),0,0},      [0x0B] = {KEY(ROW_ENTER_H,4),0,0},
    [0x0C] = {KEY(ROW_P_Y,2),0,0},      [0x0D] = {KEY(ROW_ENTER_H,3),0,0},
    [0x0E] = {KEY(ROW_ENTER_H,2),0,0},  [0x0F] = {KEY(ROW_ENTER_H,1),0,0},
    [0x10] = {KEY(ROW_SPACE_B,2),0,0},  [0x11] = {KEY(ROW_SPACE_B,3),0,0},
    [0x12] = {KEY(ROW_P_Y,1),0,0},      [0x13] = {KEY(ROW_P_Y,0),0,0},
    [0x14] = {KEY(ROW_Q_T,0),0,0},      [0x15] = {KEY(ROW_Q_T,3),0,0},
    [0x16] = {KEY(ROW_A_G,1),0,0},      [0x17] = {KEY(ROW_Q_T,4),0,0},
    [0x18] = {KEY(ROW_P_Y,3),0,0},      [0x19] = {KEY(ROW_CAPS_V,4),0,0},
    [0x1A] = {KEY(ROW_Q_T,1),0,0},      [0x1B] = {KEY(ROW_CAPS_V,2),0,0},
    [0x1C] = {KEY(ROW_P_Y,4),0,0},      [0x1D] = {KEY(ROW_CAPS_V,1),0,0},

    /* digits 1..9 then 0 */
    [0x1E] = {KEY(ROW_1_5,0),0,0}, [0x1F] = {KEY(ROW_1_5,1),0,0},
    [0x20] = {KEY(ROW_1_5,2),0,0}, [0x21] = {KEY(ROW_1_5,3),0,0},
    [0x22] = {KEY(ROW_1_5,4),0,0}, [0x23] = {KEY(ROW_0_6,4),0,0},
    [0x24] = {KEY(ROW_0_6,3),0,0}, [0x25] = {KEY(ROW_0_6,2),0,0},
    [0x26] = {KEY(ROW_0_6,1),0,0}, [0x27] = {KEY(ROW_0_6,0),0,0},

    [0x28] = {K_ENTER,0,0},
    [0x2C] = {K_SPACE,0,0},

    /* Keys a Spectrum spells as a Caps Shift combination. */
    [0x2A] = {KEY(ROW_0_6,0),1,0},   /* Backspace  = Caps Shift + 0 */
    [0x4C] = {KEY(ROW_0_6,0),1,0},   /* Delete     = the same       */
    [0x50] = {KEY(ROW_1_5,4),1,0},   /* Left       = Caps Shift + 5 */
    [0x51] = {KEY(ROW_0_6,4),1,0},   /* Down       = Caps Shift + 6 */
    [0x52] = {KEY(ROW_0_6,3),1,0},   /* Up         = Caps Shift + 7 */
    [0x4F] = {KEY(ROW_0_6,2),1,0},   /* Right      = Caps Shift + 8 */
    [0x29] = {KEY(ROW_SPACE_B,0),1,0}, /* Esc      = Caps Shift + Space, BREAK */

    /* Punctuation, which on a Spectrum is Symbol Shift plus a letter. */
    [0x33] = {KEY(ROW_P_Y,1),0,1},   /* ;  = SS+O */
    [0x34] = {KEY(ROW_0_6,3),0,1},   /* '  = SS+7 */
    [0x36] = {KEY(ROW_SPACE_B,3),0,1}, /* ,  = SS+N */
    [0x37] = {KEY(ROW_SPACE_B,2),0,1}, /* .  = SS+M */
    [0x38] = {KEY(ROW_CAPS_V,4),0,1},  /* /  = SS+V */
    [0x2D] = {KEY(ROW_ENTER_H,3),0,1}, /* -  = SS+J */
    [0x2E] = {KEY(ROW_ENTER_H,1),0,1}, /* =  = SS+L */
};

static uint8_t sMods;          /* the HID modifier byte */
static uint8_t sHeld[6];

void spectrum_keys_reset(void) {
    memset(sMatrix, 0x1F, sizeof(sMatrix));  /* all five bits high = nothing down */
    memset(sHeld, 0, sizeof(sHeld));
    sMods = 0;
}

static void press(uint8_t key) {
    if (key == NO_KEY) return;
    sMatrix[KEY_ROW(key)] &= (uint8_t)~(1 << KEY_BIT(key));
}

/* Rebuild the whole matrix from what is held. Same reasoning as the MSX
 * side: the machine scans the matrix, so recomputing it from scratch is
 * both correct and far less bug-prone than tracking press and release. */
static void rebuild(void) {
    int i;
    memset(sMatrix, 0x1F, sizeof(sMatrix));

    if (sMods & 0x22) press(K_CAPS);   /* either Shift = Caps Shift   */
    if (sMods & 0x11) press(K_SYM);    /* either Ctrl  = Symbol Shift */

    for (i = 0; i < 6; i++) {
        uint8_t hid = sHeld[i];
        if (!hid || hid >= 0x68) continue;
        if (kHid[hid].key == 0 && hid != 0x04) continue;  /* 0x04 is 'a', key 0 */
        press(kHid[hid].key);
        if (kHid[hid].caps) press(K_CAPS);
        if (kHid[hid].sym)  press(K_SYM);
    }
}

void spectrum_keys_hid(const uint8_t report[8]) {
    sMods = report[0];
    memcpy(sHeld, report + 2, 6);
}

uint8_t spectrum_keys_read(uint8_t highAddr) {
    uint8_t v = 0x1F;
    int i;
    /* A zero address line selects that half-row; several can be low at
     * once, and the machine sees them ANDed together. */
    for (i = 0; i < 8; i++)
        if (!(highAddr & (1 << i))) v &= sMatrix[i];
    return v;
}

/* ---------------------------------------------------------------- */
/* Synthetic typing, for the serial console                           */
/*                                                                    */
/* Note for anyone expecting to type a BASIC line: a 48K Spectrum is in  */
/* keyword entry mode at the start of a line, so pressing P gives PRINT  */
/* rather than the letter. That is the machine being itself, not this    */
/* code being wrong. Typing here is key-level.                           */
/* ---------------------------------------------------------------- */
#define TYPE_LEN  64
#define TYPE_HOLD 4
#define TYPE_GAP  3

static uint8_t sTypeHid[TYPE_LEN], sTypeMods[TYPE_LEN];
static uint8_t sTHead, sTTail, sTPhase, sTDown;
static uint8_t sHidForChar[128], sModsForChar[128];
static int sReverseBuilt;

static void buildReverse(void) {
    int h;
    memset(sHidForChar, 0, sizeof(sHidForChar));
    memset(sModsForChar, 0, sizeof(sModsForChar));
    for (h = 0x04; h <= 0x1D; h++) sHidForChar['a' + (h - 0x04)] = (uint8_t)h;
    for (h = 0x04; h <= 0x1D; h++) {
        sHidForChar['A' + (h - 0x04)] = (uint8_t)h;
        sModsForChar['A' + (h - 0x04)] = 0x02;   /* Caps Shift */
    }
    for (h = 0; h < 9; h++) sHidForChar['1' + h] = (uint8_t)(0x1E + h);
    sHidForChar['0'] = 0x27;
    sHidForChar[' '] = 0x2C;
    sHidForChar['\n'] = 0x28;
    sHidForChar[';'] = 0x33;  sHidForChar['\''] = 0x34;
    sHidForChar[','] = 0x36;  sHidForChar['.']  = 0x37;
    sHidForChar['/'] = 0x38;  sHidForChar['-']  = 0x2D;
    sHidForChar['='] = 0x2E;
    sReverseBuilt = 1;
}

int spectrum_keys_type(const char *text) {
    int n = 0;
    if (!sReverseBuilt) buildReverse();
    for (; *text; text++) {
        unsigned char c = (unsigned char)*text;
        uint8_t next;
        if (c >= 128 || !sHidForChar[c]) continue;
        next = (uint8_t)((sTTail + 1) % TYPE_LEN);
        if (next == sTHead) break;
        sTypeHid[sTTail]  = sHidForChar[c];
        sTypeMods[sTTail] = sModsForChar[c];
        sTTail = next;
        n++;
    }
    return n;
}

int spectrum_keys_typing(void) { return sTHead != sTTail || sTDown; }

/* Called once a frame, before the CPU runs. */
void spectrum_keys_frame(void) {
    if (sTHead != sTTail || sTDown) {
        if (sTPhase) sTPhase--;
        if (!sTPhase) {
            if (sTDown) {
                sTDown = 0;
                sTPhase = TYPE_GAP;
                sTHead = (uint8_t)((sTHead + 1) % TYPE_LEN);
            } else if (sTHead != sTTail) {
                sTDown = 1;
                sTPhase = TYPE_HOLD;
            }
        }
        memset(sHeld, 0, sizeof(sHeld));
        sMods = 0;
        if (sTDown && sTHead != sTTail) {
            sMods = sTypeMods[sTHead];
            sHeld[0] = sTypeHid[sTHead];
        }
    }
    rebuild();
}
