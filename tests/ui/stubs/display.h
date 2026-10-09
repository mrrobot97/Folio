#pragma once
#include "epd_highlevel.h"
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*,const void*,enum EpdDrawMode,EpdRect);
void guard_draw_result(EpdiyHighlevelState*,enum EpdDrawError);
