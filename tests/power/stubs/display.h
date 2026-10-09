#pragma once
#include "epd_highlevel.h"
enum EpdDrawError update_display_mode(EpdiyHighlevelState*,enum EpdDrawMode);
void guard_draw_result(EpdiyHighlevelState*,enum EpdDrawError);
uint8_t*epd_hl_get_framebuffer(EpdiyHighlevelState*);
