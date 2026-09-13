/* On the device the .tap comes from flash. Here it comes from a file. */
#include <stdio.h>
#include <stdlib.h>
static unsigned char *sImg;
static int sLen;
const unsigned char *spectrum_tape_image(void) { return sImg; }
int spectrum_tape_size(void) { return sLen; }
int tape_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }
    fseek(f, 0, SEEK_END); sLen = (int)ftell(f); fseek(f, 0, SEEK_SET);
    sImg = malloc(sLen);
    sLen = (int)fread(sImg, 1, sLen, f);
    fclose(f);
    return sLen > 0;
}
