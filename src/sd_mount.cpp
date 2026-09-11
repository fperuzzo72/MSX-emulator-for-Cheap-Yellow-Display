/* sd_mount.cpp
 *
 * Mounts the microSD slot via ESP-IDF's esp_vfs_fat_sdspi_mount so the
 * vendored fMSX core's stdio-based file I/O (fopen/fread on hardcoded
 * /sdcard/msx/... paths) works without modification. Pins are the SD
 * bus Freenove wires on the FNK0103/FNK0114N 3.5" board (separate VSPI
 * bus from the display's HSPI bus) - see README "Hardware reference".
 */
#include <Arduino.h>
#include "sd_mount.h"
#include <sys/stat.h>   /* mkdir() - not pulled in by Arduino.h */
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define SD_PIN_SCK  18
#define SD_PIN_MISO 19
#define SD_PIN_MOSI 23
#define SD_PIN_CS    5

static sdmmc_card_t *sCard = nullptr;

int sd_mount_init(void) {
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = SD_PIN_MOSI;
    bus_cfg.miso_io_num = SD_PIN_MISO;
    bus_cfg.sclk_io_num = SD_PIN_SCK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;

    esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        Serial.printf("SD: spi_bus_initialize failed (0x%x)\n", ret);
        return 0;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)SD_PIN_CS;
    slot_config.host_id = (spi_host_device_t)host.slot;

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &sCard);
    if (ret != ESP_OK) {
        Serial.printf("SD: mount failed (0x%x) - no card, or wiring issue. "
                       "Running from the BIOS embedded in flash.\n", ret);
        /* Hand the bus back: on a board with no spare RAM, the SD driver's
         * buffers are worth reclaiming when there is no card to talk to. */
        spi_bus_free((spi_host_device_t)host.slot);
        return 0;
    }

    Serial.println("SD: mounted at /sdcard");
    /* Make sure the expected directory layout exists. */
    mkdir("/sdcard/msx", 0777);
    mkdir("/sdcard/msx/bios", 0777);
    mkdir("/sdcard/msx/games", 0777);
    return 1;
}
