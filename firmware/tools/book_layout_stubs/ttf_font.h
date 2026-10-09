/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：宿主布局测试的字体契约。/ English: Font contract for host layout tests.
 * 冻结：仅供测试。/ Frozen: Tests only.
 */
#pragma once
#include "epdiy.h"
int ttf_text_width_px(int px, const char* text);
int ttf_ascender_px(int px);
int ttf_font_pixel_height_for_em(int em);
bool ttf_font_has_codepoint(uint32_t codepoint);
bool ttf_font_resource_failed(void);
void ttf_draw_text_px(uint8_t* fb, int x, int y, int px, const char* text, enum EpdFontFlags align, uint8_t fg, uint8_t bg);
