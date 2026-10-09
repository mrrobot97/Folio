/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：v0.4.2 产品页面共用几何、字体与无副作用绘制。
 * English: Shared v0.4.2 product geometry, type and side-effect-free drawing.
 * 冻结：不改波形；绘制不读硬件或持久存储。
 * Frozen: No waveform changes; drawing never reads hardware or persistent storage.
 */
#pragma once
#include "app.h"
#include <stddef.h>

#define FOLIO_SURFACE 0xEE
#define FOLIO_INK 0x11
#define FOLIO_SECONDARY 0x55
#define FOLIO_RULE 0xCC

typedef struct {
    char clock[48]; ///< 有效日期或未校时提示 / Valid local date or unset notice
    int battery_percent; ///< -1 表示未知 / -1 means unknown
    bool sd_ready; ///< 最近采样状态 / Last sampled card state
} folio_status_t;

typedef enum {
    FOLIO_ICON_HOME, FOLIO_ICON_LIBRARY, FOLIO_ICON_SETTINGS,
    FOLIO_ICON_SEARCH, FOLIO_ICON_WIFI, FOLIO_ICON_BOOKMARK,
    FOLIO_ICON_REFRESH, FOLIO_ICON_BOOK, FOLIO_ICON_SLEEP,
    FOLIO_ICON_DEVICE, FOLIO_ICON_MORE
} folio_icon_t;

/// 仅事件回调取快照，render 不调用。/ Sample only from event callbacks, never render.
void folio_ui_sample_status(folio_status_t *out);
void folio_ui_text(uint8_t *fb, int x, int y, int px, const char *text,
                   enum EpdFontFlags align, uint8_t ink, uint8_t paper);
/// UTF-8 按实际字宽截断，不切断码点。/ Ellipsize at measured UTF-8 boundaries.
void folio_ui_text_fit(uint8_t *fb, int x, int y, int px, int width,
                       const char *text, uint8_t ink, uint8_t paper);
void folio_ui_icon(uint8_t *fb, folio_icon_t icon, int x, int y, int size);
void folio_ui_status(uint8_t *fb, const folio_status_t *status);
void folio_ui_header(uint8_t *fb, const folio_status_t *status, const char *title, bool back);
void folio_ui_chevron(uint8_t *fb, int x, int y, bool left);
void folio_ui_button(uint8_t *fb, EpdRect hit, const char *label, bool primary);
void folio_ui_row(uint8_t *fb, int y, const char *label, const char *value, uint8_t paper);
void folio_ui_tabs(uint8_t *fb, int selected);
int folio_ui_tab_hit(int x, int y);
void folio_ui_duration(uint64_t ms, char *out, size_t cap);
/// 有效年月日（日历）工具；公历，不依赖时区。/ Gregorian calendar helpers, timezone independent.
bool folio_ui_date_valid(int day);
int folio_ui_month_days(int year, int month);
int folio_ui_previous_day(int day);
int folio_ui_calendar_offset(int year, int month);
EpdRect folio_ui_day_rect(int year, int month, int day);
/// 只给出独立的区域，不将不相邻区域合并。/ Keep disjoint regions separate.
size_t folio_ui_selection_regions(int year, int month, int old_day, int new_day, EpdRect out[3]);
