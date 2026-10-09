/* SPDX-License-Identifier: Apache-2.0
 * 只解码固定尺寸 4bpp，拒绝截断/扩展/CRC 损坏，不运行外部内容。
 * Decode fixed-size 4bpp only; reject truncation, tails and CRC corruption; no external content execution.
 */
#include "wallpaper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
static uint32_t u32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static bool header_valid(const uint8_t* file) {
    return !memcmp(file,"FWP1",4) && (file[4] | (unsigned)file[5]<<8) == FOLIO_WALLPAPER_WIDTH &&
        (file[6] | (unsigned)file[7]<<8) == FOLIO_WALLPAPER_HEIGHT && u32(file+8) == FOLIO_WALLPAPER_BYTES;
}
bool folio_wallpaper_decode(const uint8_t* file, size_t bytes, uint8_t* out, size_t capacity) {
    if (!file || !out || bytes != FOLIO_WALLPAPER_HEADER + FOLIO_WALLPAPER_BYTES ||
        capacity < FOLIO_WALLPAPER_BYTES || !header_valid(file) ||
        u32(file+12) != crc32(file+16,FOLIO_WALLPAPER_BYTES)) return false;
    memmove(out,file+16,FOLIO_WALLPAPER_BYTES); return true;
}
bool folio_wallpaper_path_valid(const char* path) {
    static const char prefix[] = "/sdcard/wallpapers/";
    if (!path || strlen(path) >= 160 || strncmp(path,prefix,sizeof(prefix)-1)) return false;
    const char* name = path+sizeof(prefix)-1;
    size_t n = strlen(name);
    if (n <= 4 || strcmp(name+n-4,".fwp")) return false;
    for (const unsigned char* p = (const unsigned char*)name; *p; ++p)
        if (*p < 32 || *p == 127 || *p == '/' || *p == '\\' || *p == ':') return false;
    return true;
}
bool folio_wallpaper_read(const char* path, uint8_t** out) {
    if (!out || !folio_wallpaper_path_valid(path)) return false;
    FILE* f = fopen(path,"rb");
    if (!f) return false;
    uint8_t header[16]; uint8_t* pixels = NULL;
    if (fread(header,1,sizeof(header),f) != sizeof(header) || !header_valid(header)) { fclose(f); return false; }
#ifdef ESP_PLATFORM
    pixels = heap_caps_malloc(FOLIO_WALLPAPER_BYTES,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    pixels = malloc(FOLIO_WALLPAPER_BYTES);
#endif
    if (!pixels) { fclose(f); return false; }
    bool valid = fread(pixels,1,FOLIO_WALLPAPER_BYTES,f) == FOLIO_WALLPAPER_BYTES &&
        fgetc(f) == EOF && !ferror(f) && u32(header+12) == crc32(pixels,FOLIO_WALLPAPER_BYTES);
    if (fclose(f)) valid = false;
    if (!valid) { free(pixels); return false; }
    *out = pixels; return true;
}
