/* SPDX-License-Identifier: Apache-2.0 */
#include "wallpaper.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char** argv) {
    assert(argc == 2);
    size_t n = FOLIO_WALLPAPER_BYTES+16;
    uint8_t* input = malloc(n+1), *out = malloc(FOLIO_WALLPAPER_BYTES);
    assert(input && out);
    FILE* f = fopen(argv[1],"rb"); assert(f && fread(input,1,n,f) == n); fclose(f);
    assert(folio_wallpaper_decode(input,n,out,FOLIO_WALLPAPER_BYTES));
    for (size_t i = 0; i < FOLIO_WALLPAPER_BYTES; ++i) assert(out[i] == (uint8_t)i);
    assert(!folio_wallpaper_decode(input,n-1,out,FOLIO_WALLPAPER_BYTES));
    assert(!folio_wallpaper_decode(input,n+1,out,FOLIO_WALLPAPER_BYTES));
    assert(!folio_wallpaper_decode(input,n,out,FOLIO_WALLPAPER_BYTES-1));
    const int corrupt[] = {0,4,6,8,12,16};
    for (unsigned i = 0; i < sizeof(corrupt)/sizeof(corrupt[0]); ++i) {
        input[corrupt[i]] ^= 1; memset(out,0x7b,FOLIO_WALLPAPER_BYTES);
        assert(!folio_wallpaper_decode(input,n,out,FOLIO_WALLPAPER_BYTES));
        assert(out[0] == 0x7b && out[FOLIO_WALLPAPER_BYTES-1] == 0x7b);
        input[corrupt[i]] ^= 1;
    }
    assert(folio_wallpaper_path_valid("/sdcard/wallpapers/云.fwp"));
    const char* bad[] = {"/sdcard/wallpapers2/x.fwp","/sdcard/wallpapers/../x.fwp","/sdcard/wallpapers/.fwp",
        "/sdcard/wallpapers/x.txt","/sdcard/wallpapers/sub/x.fwp","/sdcard/wallpapers/x\\y.fwp",NULL};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) assert(!folio_wallpaper_path_valid(bad[i]));
    free(input);free(out);
    puts("wallpaper: reference CRC, bounds, corruption, output preservation and path isolation passed");
}
