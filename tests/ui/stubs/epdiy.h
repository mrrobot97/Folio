#pragma once
#include <stdint.h>
typedef struct {int x,y,width,height;} EpdRect;
enum EpdFontFlags {EPD_DRAW_ALIGN_LEFT,EPD_DRAW_ALIGN_CENTER,EPD_DRAW_ALIGN_RIGHT};
enum EpdDrawMode {MODE_DU=1,MODE_GL16=2,MODE_GC16=3};
enum EpdDrawError {EPD_DRAW_SUCCESS=0};
void epd_draw_pixel(int,int,uint8_t,uint8_t*);
void epd_draw_line(int,int,int,int,uint8_t,uint8_t*);
void epd_draw_circle(int,int,int,uint8_t,uint8_t*);
void epd_fill_circle(int,int,int,uint8_t,uint8_t*);
void epd_fill_rect(EpdRect,uint8_t,uint8_t*);
