/* audio_glue.c - the audio backend EMULib expects a port to supply.
 *
 * Sound.c calls InitAudio/WriteAudio/GetFreeAudio/TrashAudio; every fMSX
 * port implements them against whatever the platform has. This board's
 * speaker hangs off the ESP32's built-in DAC (Freenove's own MP3 example
 * for it constructs AudioOutputI2S(0, 1), i.e. I2S port 0 in internal-DAC
 * mode), so that is what this drives: I2S in DAC mode, which streams
 * samples to GPIO25/26 in the background without the emulator having to
 * keep time.
 *
 * The PSG/SCC/OPLL mixing all happens inside the core already. What was
 * missing was somewhere to put the result.
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s.h"

#include "EMULib.h"
#include "Sound.h"

#define AUDIO_PORT      I2S_NUM_0
#define AUDIO_DMA_BUFS  4
#define AUDIO_DMA_LEN   256   /* samples per DMA buffer */

static int sRate = 0;
static int sPaused = 0;

unsigned int InitAudio(unsigned int Rate, unsigned int Latency) {
    i2s_config_t cfg;

    if (!Rate) return 0;
    /* 22050Hz is as much as this board can keep up with while emulating a
     * Z80 and pushing 256x212 pixels over SPI on the same core. */
    if (Rate > 22050) Rate = 22050;

    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
    cfg.sample_rate = Rate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    cfg.intr_alloc_flags = 0;
    cfg.dma_buf_count = AUDIO_DMA_BUFS;
    cfg.dma_buf_len = AUDIO_DMA_LEN;
    cfg.use_apll = false;

    if (i2s_driver_install(AUDIO_PORT, &cfg, 0, NULL) != ESP_OK) return 0;
    i2s_set_pin(AUDIO_PORT, NULL); /* NULL pin config = built-in DAC */
    i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    i2s_zero_dma_buffer(AUDIO_PORT);

    sRate = (int)Rate;
    sPaused = 0;
    (void)Latency;
    return Rate;
}

void TrashAudio(void) {
    if (!sRate) return;
    i2s_driver_uninstall(AUDIO_PORT);
    sRate = 0;
}

unsigned int GetTotalAudio(void) {
    return sRate ? AUDIO_DMA_BUFS * AUDIO_DMA_LEN : 0;
}

unsigned int GetFreeAudio(void) {
    /* i2s_write() blocks when the DMA chain is full, and the emulator only
     * hands us a frame's worth at a time, so reporting the whole buffer as
     * free is both true enough and keeps the core from throttling itself. */
    return (sRate && !sPaused) ? GetTotalAudio() : 0;
}

unsigned int WriteAudio(sample *Data, unsigned int Length) {
    /* The DAC wants unsigned samples in the top 8 bits of each 16-bit
     * word, and both channels written; the core hands us signed mono. */
    static unsigned short conv[256];
    unsigned int done = 0;
    size_t wrote;

    if (!sRate || sPaused || !Length) return 0;

    while (done < Length) {
        unsigned int n = Length - done;
        unsigned int i;
        if (n > sizeof(conv) / sizeof(conv[0]) / 2) n = sizeof(conv) / sizeof(conv[0]) / 2;
        for (i = 0; i < n; i++) {
            unsigned short v = (unsigned short)((int)Data[done + i] + 32768);
            conv[i * 2]     = v; /* right channel = GPIO25 */
            conv[i * 2 + 1] = v; /* left channel  = GPIO26 */
        }
        if (i2s_write(AUDIO_PORT, conv, n * 2 * sizeof(unsigned short), &wrote,
                      pdMS_TO_TICKS(2)) != ESP_OK)
            break;
        if (!wrote) break;
        done += wrote / (2 * sizeof(unsigned short));
    }
    return done;
}

int PauseAudio(int Switch) {
    if (Switch == 2) Switch = !sPaused;   /* 2 = toggle, per EMULib */
    if (Switch >= 0) {
        sPaused = Switch ? 1 : 0;
        if (sPaused && sRate) i2s_zero_dma_buffer(AUDIO_PORT);
    }
    return sPaused;
}
