/* SPDX-License-Identifier: Apache-2.0
 * v0.4.2 排版页的共用几何与纯绘制。/ Shared v0.4.2 format geometry and pure drawing.
 */
#pragma once
#include "folio_ui.h"

/// 字段页固定操作区；候选合并不等于应用。/ Fixed field actions; keeping a candidate never means applying.
void folio_format_ui_footer(uint8_t *fb, const char *keep_label);
/// 绘制与命中共用选择项矩形。/ Choice rectangle shared by drawing and hit-testing.
EpdRect folio_format_ui_choice_rect(int index, int count, int y);
void folio_format_ui_choice(uint8_t *fb, EpdRect rect, const char *label, bool selected);
void folio_format_ui_radio(uint8_t *fb, EpdRect rect, const char *label, const char *detail, bool selected);
/// 公共返回点击，包含左下取消。/ Common back hit including bottom-left Cancel.
bool folio_format_ui_cancel_hit(int x, int y);

#include "format_model.h"
typedef struct {
    bool ready, builtin, sample_complete, variable_weight;
    int weight_min, weight_max;
    uint32_t first_missing;
    char name[96];
} folio_format_preview_t;
/// 事件阶段打开候选并核对样例缺字；不改变 READER。/ Event-stage candidate load and sample coverage check, preserving READER.
bool folio_format_ui_prepare(const folio_format_values_t *values, folio_format_preview_t *preview);
/// 已加载预览的纯绘制；不会打开字体或更改活动排版。/ Pure loaded-preview rendering; never open fonts or activate formatting.
void folio_format_ui_preview(uint8_t *fb, EpdRect box, const folio_format_values_t *values,
    const folio_format_preview_t *preview, const char *first, const char *second, bool caption, bool paragraph_layout);
const char *folio_format_ui_background(unsigned background);
const char *folio_format_ui_weight(unsigned weight);
void folio_format_ui_font_name(const char *path, char *out, size_t capacity);
