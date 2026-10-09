#pragma once
#include <stdint.h>
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT=0,EPD_DRAW_ALIGN_CENTER=1,EPD_DRAW_ALIGN_RIGHT=2};
void epd_draw_pixel(int,int,uint8_t,uint8_t*);
