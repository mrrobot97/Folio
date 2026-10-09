/* SPDX-License-Identifier: Apache-2.0
 * 手动校时页面；草稿不影响阅读统计，确认后才采用。
 * Manual clock page: edits remain a draft until explicitly applied.
 */
#include "app.h"
#include "folio_clock.h"
#include "folio_ui.h"
#include "ui_kit.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include <stdio.h>

static int s_values[5];
static folio_status_t s_status;
static const char* s_result;
static void enter(app_ctx_t* ctx) {
    (void)ctx;
    // 未校时的表单只是待编辑默认值，不是当前日期。/ An unset form is an editable default, never a current date claim.
    s_values[0] = 2026; s_values[1] = 1; s_values[2] = 1; s_values[3] = 12; s_values[4] = 0;
    folio_clock_local(&s_values[0], &s_values[1], &s_values[2], &s_values[3], &s_values[4]);
    folio_ui_sample_status(&s_status); s_result = "调整后点确认才会开始按日期统计";
}
static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    folio_ui_header(fb, &s_status, "日期与时间", true);
    folio_ui_text(fb, 40, 188, 28, "本地时区 UTC+8", EPD_DRAW_ALIGN_LEFT, FOLIO_SECONDARY, 0xFF);
    const char* labels[] = {"年", "月", "日", "时", "分"};
    for (int i = 0; i < 5; ++i) {
        int y = 254 + 118 * i; char value[24];
        snprintf(value, sizeof(value), "%d %s", s_values[i], labels[i]);
        folio_ui_text(fb, 342, y + 25, 38, value, EPD_DRAW_ALIGN_CENTER, FOLIO_INK, 0xFF);
        folio_ui_button(fb, (EpdRect){40,y,110,96}, "−", false);
        folio_ui_button(fb, (EpdRect){534,y,110,96}, "+", false);
    }
    folio_ui_text_fit(fb,40,884,26,604,s_result,FOLIO_SECONDARY,0xFF);
    folio_ui_text_fit(fb,40,930,26,604,"ECO 断电后需重新校时，旧记录保留。",FOLIO_SECONDARY,0xFF);
    folio_ui_button(fb,(EpdRect){40,1030,284,96},"返回",false);
    folio_ui_button(fb,(EpdRect){360,1030,284,96},"确认时间",true);
}
static app_redraw_t gesture(app_ctx_t* ctx, const ui_gesture_event_t* e) {
    if (e->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    if (ui_rect_hit((EpdRect){0,78,110,96},e->x0,e->y0) || ui_rect_hit((EpdRect){40,1030,284,96},e->x0,e->y0)) {
        ctx->request_return = true; return APP_REDRAW_NONE;
    }
    for (int i = 0; i < 5; ++i) {
        int y = 254 + 118 * i, delta = 0;
        if (ui_rect_hit((EpdRect){40,y,110,96},e->x0,e->y0)) delta = -1;
        if (ui_rect_hit((EpdRect){534,y,110,96},e->x0,e->y0)) delta = 1;
        if (!delta) continue;
        int min[] = {2020,1,1,0,0}, max[] = {2099,12,folio_clock_month_days(s_values[0],s_values[1]),23,59};
        s_values[i] += delta;
        if (s_values[i] < min[i]) s_values[i] = min[i];
        if (s_values[i] > max[i]) s_values[i] = max[i];
        int days = folio_clock_month_days(s_values[0],s_values[1]);
        if (s_values[2] > days) s_values[2] = days;
        return APP_REDRAW_PAGE;
    }
    if (ui_rect_hit((EpdRect){360,1030,284,96},e->x0,e->y0)) {
        s_result = folio_clock_set_local(s_values[0],s_values[1],s_values[2],s_values[3],s_values[4]) ?
            "时间已设置；未标日期的旧记录保持不变。" : "日期无效，请检查";
        folio_ui_sample_status(&s_status); return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t key(app_ctx_t* ctx, int key) {
    if (key == UI_KEY_1) ctx->request_return = true;
    if (key == UI_KEY_2) return APP_REDRAW_FULL;
    if (key == UI_KEY_3) ctx->request_menu = true;
    return APP_REDRAW_NONE;
}
const app_desc_t app_folio_datetime = {.title="日期与时间", .detail="手动校时与离线统计", .owns_keys=true,.owns_footer=true,
    .on_enter=enter, .render=render, .on_gesture=gesture, .on_key=key};
