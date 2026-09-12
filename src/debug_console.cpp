/* debug_console.cpp - serial self-test console. See debug_console.h.
 *
 * Commands (one per line, 115200 8N1):
 *   s            dump the emulated text screen, non-ASCII as {XX}
 *   t <text>     type text as if on the US-International keyboard, so
 *                "t 'a" composes an a-acute through the real dead-key path
 *   d <n> <s> <c>  press dead-key position n (0-2), shift s (0/1), then
 *                character c - the experiment that identifies the accents
 *   h            heap and status
 *   ?            this list
 */
#include <Arduino.h>
#include "debug_console.h"
#include "msx_bridge.h"
#include "msx_keys.h"
#include "ble_keyboard.h"
#include "sd_mount.h"

extern "C" void display_request_test_pattern(int which);
extern "C" void display_set_swap_bytes(int on);
extern "C" void display_set_scale(int scale);
extern "C" void audio_test_tone(int hz, int ms);
extern "C" unsigned long audio_samples_written(void);
extern "C" unsigned long display_blit_us(void);
extern "C" void display_blit_us_reset(void);
extern "C" int  display_get_scale(void);

/* The handful of MSX international-charset codes worth naming when they
 * turn up in a screen dump; everything else prints as its hex code. */
static const char *charName(uint8_t c) {
    switch (c) {
        case 0x80: return "C,"; case 0x87: return "c,";   /* C-cedilla    */
        case 0x82: return "e'"; case 0x90: return "E'";
        case 0xA0: return "a'"; case 0x83: return "a^";
        case 0x85: return "a`"; case 0x84: return "a\"";
        case 0x88: return "e^"; case 0x89: return "e\"";
        case 0x8A: return "e`"; case 0xA1: return "i'";
        case 0x8C: return "i^"; case 0x8B: return "i\"";
        case 0xA2: return "o'"; case 0x93: return "o^";
        case 0x95: return "o`"; case 0x94: return "o\"";
        case 0xA3: return "u'"; case 0x96: return "u^";
        case 0x97: return "u`"; case 0x81: return "u\"";
        case 0xA4: return "n~"; case 0xA5: return "N~";
        case 0xA6: return "a~"; case 0xA7: return "o~";
        default:   return nullptr;
    }
}

static void dumpScreen() {
    uint8_t row[48];
    int mode = msx_screen_mode();
    Serial.printf("screen mode %d\n", mode);
    for (int y = 0; y < 24; y++) {
        int cols = msx_screen_row(y, row, sizeof(row));
        if (cols <= 0) { Serial.println("(not a text mode)"); return; }
        Serial.printf("%2d |", y);
        for (int x = 0; x < cols; x++) {
            uint8_t c = row[x];
            if (c >= 0x20 && c < 0x7F) Serial.write(c);
            else if (c == 0x00 || c == 0x20) Serial.write(' ');
            else {
                const char *n = charName(c);
                if (n) Serial.printf("[%s]", n);
                else   Serial.printf("{%02X}", c);
            }
        }
        Serial.println("|");
    }
}

static void handleLine(char *line) {
    switch (line[0]) {
        case 's':
            dumpScreen();
            break;
        case 't': {
            /* "\n" in the text is a real Return, so a whole BASIC line
             * can be typed and entered in one command. */
            char *text = line[1] == ' ' ? line + 2 : line + 1;
            char *r = text, *w = text;
            while (*r) {
                if (r[0] == '\\' && r[1] == 'n') { *w++ = '\n'; r += 2; }
                else *w++ = *r++;
            }
            *w = 0;
            int n = msx_keys_type(text);
            Serial.printf("typing %d chars\n", n);
            break;
        }
        case 'd': {
            int which = 0, shift = 0; char base = 'a';
            /* "d 0 1 a" */
            if (sscanf(line + 1, "%d %d %c", &which, &shift, &base) >= 1) {
                msx_keys_probe_dead(which, shift, base);
                Serial.printf("dead-key probe: position %d, shift %d, base '%c'\n",
                              which, shift, base);
            }
            break;
        }
        case 'g': {
            int code = 0;
            if (sscanf(line + 1, "%i", &code) == 1) {
                uint8_t rows[8];
                if (!msx_char_pattern(code, rows)) { Serial.println("no pattern table"); break; }
                Serial.printf("char 0x%02X:\n", code);
                for (int y = 0; y < 8; y++) {
                    char out[10];
                    for (int x = 0; x < 8; x++) out[x] = (rows[y] & (0x80 >> x)) ? '#' : '.';
                    out[8] = 0;
                    Serial.printf("  %s\n", out);
                }
            }
            break;
        }
        case 'x': {
            int which = 0;
            sscanf(line + 1, "%d", &which);
            display_request_test_pattern(which);
            Serial.printf("test pattern %d drawn (0 black, 1 red, 2 green, 3 blue, "
                          "4 white block via TFT_eSPI, 5 same block via the emulator's path)\n", which);
            break;
        }
        case 'b': {
            int on = 1;
            sscanf(line + 1, "%d", &on);
            ble_keyboard_scan(on);
            Serial.printf("BLE scan %s (%lu adverts seen)\n",
                          on ? "on" : "off", ble_keyboard_adverts_seen());
            break;
        }
        case 'n': {
            int on = 1;
            sscanf(line + 1, "%d", &on);
            msx_set_sound(on);
            Serial.printf("sound %s\n", on ? "on" : "off");
            break;
        }
        case 'a': {
            int hz = 440, ms = 600;
            sscanf(line + 1, "%d %d", &hz, &ms);
            Serial.printf("test tone %d Hz for %d ms (samples sent so far: %lu)\n",
                          hz, ms, audio_samples_written());
            audio_test_tone(hz, ms);
            Serial.println("tone done");
            break;
        }
        case 'r': {
            int addr = 0, len = 8;
            if (sscanf(line + 1, "%i %i", &addr, &len) >= 1) {
                if (len < 1) len = 1;
                if (len > 64) len = 64;
                Serial.printf("%04X:", addr);
                for (int i = 0; i < len; i++) Serial.printf(" %02X", msx_peek(addr + i));
                Serial.println();
            }
            break;
        }
        case 'm': {
            int on = 1;
            sscanf(line + 1, "%d", &on);
            if (on) {
                Serial.printf("mounting the card: %s\n",
                              sd_mount_init() ? "ok" : "failed");
            } else {
                sd_unmount();
            }
            Serial.printf("free heap %u\n", msx_free_heap());
            break;
        }
        case 'p': {
            int row = 0, bit = 0, mods = 0;
            if (sscanf(line + 1, "%i %i %i", &row, &bit, &mods) >= 2) {
                msx_keys_press_matrix(row, bit, mods);
                Serial.printf("pressed row %d bit 0x%02X mods %d (1=Shift 2=Ctrl)\n",
                              row, bit, mods);
            }
            break;
        }
        case 'k': {
            int on = 1;
            sscanf(line + 1, "%d", &on);
            ble_keyboard_log_reports(on);
            Serial.printf("HID report dump %s, %lu report(s) received so far\n",
                          on ? "on" : "off", ble_keyboard_report_count());
            break;
        }
        case 'z': {
            int scale = 0;
            if (sscanf(line + 1, "%d", &scale) != 1 || (scale != 1 && scale != 2))
                scale = display_get_scale() == 1 ? 2 : 1;   /* bare 'z' toggles */
            display_set_scale(scale);
            Serial.printf("picture scale %s\n",
                          scale == 1 ? "1:1 (256x216, crisp, small)"
                                     : "1.5x (384x324, nearly full screen)");
            break;
        }
        case 'w': {
            int on = 1;
            sscanf(line + 1, "%d", &on);
            display_set_swap_bytes(on);
            Serial.printf("colour byte swap %s\n", on ? "on" : "off");
            break;
        }
        case 'h': {
            static unsigned int lastFrames = 0, lastFrames0 = 0;
            static unsigned long lastMs = 0;
            unsigned int frames = msx_frame_count();
            unsigned long now = millis();
            float fps = (lastMs && now > lastMs)
                        ? (frames - lastFrames) * 1000.0f / (now - lastMs) : 0.0f;
            unsigned long blitUs = display_blit_us();
            display_blit_us_reset();
            lastFrames = frames; lastMs = now;
            Serial.printf("free heap %u, keyboard %s, %.1f fps since last 'h', "
                          "typing %d, pending accent '%s'\n",
                          msx_free_heap(),
                          ble_keyboard_connected() ? "connected" : "not connected",
                          fps, msx_keys_typing(), msx_keys_pending_accent());
            Serial.printf("HID reports received: %lu, audio samples sent: %lu\n",
                          ble_keyboard_report_count(), audio_samples_written());
            Serial.printf("full-panel repaints: %lu\n", msx_full_repaints());
            if (fps > 0.0f && frames > lastFrames0) {
                float perFrameUs = (float)blitUs / (float)(frames - lastFrames0);
                Serial.printf("blit %.1f ms/frame, %.0f%% of a %.1f ms frame\n",
                              perFrameUs / 1000.0f,
                              100.0f * perFrameUs / (1000000.0f / fps),
                              1000.0f / fps);
            }
            lastFrames0 = frames;
            break;
        }
        case '?':
        default:
            Serial.println("s=screen  t <text>=type  d=dead-key probe  g <code>=glyph  b <0|1>=BLE scan on/off  n <0|1>=sound on/off  a [hz] [ms]=test tone  r <addr> [len]=peek MSX memory  m <0|1>=unmount/mount the card  p <row> <bit> <mods>=press a matrix key  k [0|1]=dump HID reports  z [1|2]=picture scale  x <n>=test pattern  w <0|1>=byte swap  h=status");
            break;
    }
}

static void consoleTask(void *arg) {
    (void)arg;
    char line[160];
    size_t len = 0;
    for (;;) {
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                line[len] = 0;
                if (len) handleLine(line);
                len = 0;
            } else if (len < sizeof(line) - 1) {
                line[len++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void debug_console_init(void) {
    /* Core 0: core 1 belongs to the emulator and its video task, both at
     * priority 5, and a console pinned there would rarely get to run. */
    xTaskCreatePinnedToCore(consoleTask, "console", 4096, NULL, 2, NULL, 0);
}
