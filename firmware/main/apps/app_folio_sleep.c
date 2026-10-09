/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：S04/S04-FAST/S04-PICKUP 设置与电源请求；不直接调用电源驱动。
 * English: S04/S04-FAST/S04-PICKUP preferences and power requests; no direct power-driver calls.
 * 冻结：只有 FAST 且传感器可用时允许拿起唤醒；电源操作由统一生命周期执行。
 * Frozen: Pickup requires FAST and a ready sensor; unified lifecycle owns power changes.
 */
#include "app.h"
#include "folio_ui.h"
#include "folio_navigation.h"
#include "ui_kit.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "settings.h"

extern const app_desc_t app_folio_wallpaper;
typedef enum { SLEEP_OPTIONS, SLEEP_PICKUP, SLEEP_OFF_CONFIRM } sleep_view_t;
static struct {folio_status_t status;sleep_view_t view;app_sleep_mode_t mode;bool pickup,sensor,save_failed,custom_wallpaper;} s_sleep;
static void sample(app_ctx_t *ctx){
    folio_ui_sample_status(&s_sleep.status);s_sleep.mode=app_settings_sleep_mode();s_sleep.pickup=app_settings_pickup_wake();
    s_sleep.sensor=ctx->sensor_ready;s_sleep.save_failed=app_settings_storage_error()!=ESP_OK;
    s_sleep.custom_wallpaper=app_settings_wallpaper_path()[0]!=0;
}
static void on_enter(app_ctx_t *ctx){s_sleep.view=SLEEP_OPTIONS;sample(ctx);}
static void choice(uint8_t *fb,int y,const char *label,const char *detail,bool selected){
    epd_draw_circle(61,y+35,12,FOLIO_INK,fb);
    if(selected)epd_fill_circle(61,y+35,7,FOLIO_INK,fb);
    folio_ui_text(fb,96,y+14,34,label,EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
    folio_ui_text(fb,96,y+62,26,detail,EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
}
static void render(app_ctx_t *ctx,uint8_t *fb){
    (void)ctx;folio_ui_header(fb,&s_sleep.status,s_sleep.view==SLEEP_PICKUP?"拿起唤醒":s_sleep.view==SLEEP_OFF_CONFIRM?"关机确认":"休眠与唤醒",true);
    if(s_sleep.view==SLEEP_OPTIONS){
        folio_ui_text(fb,40,232,34,"自动休眠",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,590,236,26,"暂未启用",EPD_DRAW_ALIGN_RIGHT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,278,26,"可按电源键或下方按钮手动休眠。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        choice(fb,384,"省电 · ECO","重新开机后恢复已保存位置",s_sleep.mode==APP_SLEEP_DEEP);
        choice(fb,518,"快速唤醒 · FAST","直接回原页，待机耗电较高",s_sleep.mode==APP_SLEEP_LIGHT);
        folio_ui_text(fb,40,716,34,"拿起唤醒",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        const char *note=s_sleep.mode!=APP_SLEEP_LIGHT?"请先选择快速唤醒，才可开启此项。":!s_sleep.sensor?"传感器不可用，当前保持关闭。":"仅在快速唤醒模式可用";
        folio_ui_text(fb,40,764,26,note,EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        if(s_sleep.mode==APP_SLEEP_LIGHT&&s_sleep.sensor){
            folio_ui_text(fb,590,720,26,s_sleep.pickup?"开启":"关闭",EPD_DRAW_ALIGN_RIGHT,FOLIO_SECONDARY,0xFF);folio_ui_chevron(fb,623,738,false);
        }
        folio_ui_row(fb,824,"休眠壁纸",s_sleep.custom_wallpaper?"自定义":"内置",0xFF);
        folio_ui_button(fb,(EpdRect){40,942,290,96},"立即休眠",false);
        folio_ui_button(fb,(EpdRect){346,942,298,96},"关机",false);
        if(s_sleep.mode==APP_SLEEP_OFF)folio_ui_text(fb,40,1048,24,"当前电源键模式为关机，可选 FAST 或 ECO。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    }else if(s_sleep.view==SLEEP_PICKUP){
        choice(fb,226,"关闭","默认，更少误唤醒",!s_sleep.pickup);
        choice(fb,354,"开启","拿起设备时唤醒",s_sleep.pickup&&s_sleep.sensor);
        folio_ui_text(fb,40,898,28,"只在快速唤醒模式使用。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,950,28,"传感器不可用时保持关闭。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    }else{
        folio_ui_text(fb,40,236,38,"保存成功后关机",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,344,28,"关机前暂停计时并保存阅读记录。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,410,28,"保存失败时会取消断电。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,476,28,"关机后长按电源键重新开机。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_button(fb,(EpdRect){40,942,604,96},"确认关机",true);
    }
    if(s_sleep.save_failed)folio_ui_text_fit(fb,40,1072,24,604,"设置保存失败，仍使用原设置。",FOLIO_INK,0xFF);
    folio_ui_button(fb,(EpdRect){40,1100,604,96},s_sleep.view==SLEEP_OPTIONS?"返回设置":s_sleep.view==SLEEP_PICKUP?"完成":"取消",false);
}
static app_redraw_t back(app_ctx_t *ctx){
    if(s_sleep.view!=SLEEP_OPTIONS){s_sleep.view=SLEEP_OPTIONS;return APP_REDRAW_PAGE;}
    ctx->request_return=true;return APP_REDRAW_NONE;
}
static app_redraw_t on_gesture(app_ctx_t *ctx,const ui_gesture_event_t *e){
    if(e->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    int x=e->x0,y=e->y0;
    if(ui_rect_hit((EpdRect){24,78,96,96},x,y)||ui_rect_hit((EpdRect){40,1100,604,96},x,y))return back(ctx);
    if(s_sleep.view==SLEEP_OPTIONS){
        if(ui_rect_hit((EpdRect){32,384,620,112},x,y)){app_settings_set_sleep_mode(APP_SLEEP_DEEP);sample(ctx);return APP_REDRAW_PAGE;}
        if(ui_rect_hit((EpdRect){32,518,620,112},x,y)){app_settings_set_sleep_mode(APP_SLEEP_LIGHT);sample(ctx);return APP_REDRAW_PAGE;}
        if(s_sleep.mode==APP_SLEEP_LIGHT&&s_sleep.sensor&&ui_rect_hit((EpdRect){32,700,620,112},x,y)){s_sleep.view=SLEEP_PICKUP;return APP_REDRAW_PAGE;}
        if(ui_rect_hit((EpdRect){32,824,620,112},x,y)){ctx->request_app=&app_folio_wallpaper;return APP_REDRAW_NONE;}
        if(ui_rect_hit((EpdRect){40,942,290,96},x,y)){ctx->request_sleep=true;return APP_REDRAW_NONE;}
        if(ui_rect_hit((EpdRect){346,942,298,96},x,y)){s_sleep.view=SLEEP_OFF_CONFIRM;return APP_REDRAW_PAGE;}
    }else if(s_sleep.view==SLEEP_PICKUP){
        if(ui_rect_hit((EpdRect){32,226,620,112},x,y)){app_settings_set_pickup_wake(false);sample(ctx);return APP_REDRAW_PAGE;}
        if(s_sleep.mode==APP_SLEEP_LIGHT&&s_sleep.sensor&&ui_rect_hit((EpdRect){32,354,620,112},x,y)){app_settings_set_pickup_wake(true);sample(ctx);return APP_REDRAW_PAGE;}
    }else if(ui_rect_hit((EpdRect){40,942,604,96},x,y)){ctx->request_poweroff=true;return APP_REDRAW_NONE;}
    return APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t *ctx,int key){
    if(key==UI_KEY_1)return back(ctx);
    if(key==UI_KEY_2)return APP_REDRAW_FULL;
    if(key==UI_KEY_3)ctx->request_menu=true;
    return APP_REDRAW_NONE;
}
const app_desc_t app_folio_sleep={.title="休眠与唤醒",.detail="FAST / ECO 与壁纸",.owns_keys=true,.owns_footer=true,
    .on_enter=on_enter,.render=render,.on_gesture=on_gesture,.on_key=on_key};
