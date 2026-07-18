#ifndef SD_MOUNT_H
#define SD_MOUNT_H
#ifdef __cplusplus
extern "C" {
#endif

/* Mounts the SD card (SPI: SCK18/MISO19/MOSI23/CS5, see README) at
 * /sdcard using ESP-IDF's FAT+VFS SD driver, so plain fopen()/fread()
 * calls inside the vendored fMSX core (which hardcodes /sdcard/msx/...
 * paths) work unmodified. Returns 1 on success, 0 if no card is present
 * (fMSX will then fall back to the embedded C-BIOS for the main BIOS, but
 * cartridge games still require a working SD card). */
int sd_mount_init(void);

#ifdef __cplusplus
}
#endif
#endif
