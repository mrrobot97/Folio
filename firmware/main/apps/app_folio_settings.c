/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：S01 分组设置及阅读、刷新、设备帮助入口。
 * English: S01 grouped settings and reading, refresh and device-help entry points.
 * 冻结：无全局深色模式；不写屏幕/PMU危险寄存器；render 无副作用。
 * Frozen: No global dark mode, no hazardous panel/PMU register writes, pure render.
 */
#include "app.h"
#include "folio_metrics.h"
#include "folio_ui.h"
#include "folio_navigation.h"
#include "reader_runtime.h"
#include "format_service.h"
#include "ui_kit.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "settings.h"
#include <stdio.h>

extern const app_desc_t app_transfer,app_font_pick,app_sd,app_home;
extern const app_desc_t app_folio_wallpaper,app_folio_datetime;
typedef enum { ROOT, READING, REFRESH, SLEEP, STORAGE, HELP } settings_view_t;
static struct { folio_status_t status; settings_view_t view; int px; bool shake,pickup,storage_error;app_sleep_mode_t sleep;char format_error[192];} s_settings;
static void sample(void){
    folio_ui_sample_status(&s_settings.status);s_settings.px=app_settings_book_px();s_settings.shake=app_settings_book_shake();
    s_settings.storage_error=app_settings_storage_error()!=ESP_OK;
    s_settings.pickup=app_settings_pickup_wake();s_settings.sleep=app_settings_sleep_mode();
}
static void on_enter(app_ctx_t *ctx){(void)ctx;s_settings.format_error[0]=0;sample();}
static void group_row(uint8_t *fb,int y,folio_icon_t icon,const char *label,const char *value,bool rule){
    folio_ui_icon(fb,icon,58,y+29,34);
    folio_ui_text(fb,114,y+28,34,label,EPD_DRAW_ALIGN_LEFT,FOLIO_INK,FOLIO_SURFACE);
    if(value)folio_ui_text(fb,591,y+32,26,value,EPD_DRAW_ALIGN_RIGHT,FOLIO_SECONDARY,FOLIO_SURFACE);
    folio_ui_chevron(fb,619,y+49,false);
    if(rule)ui_hairline(fb,y+105,114,502,FOLIO_RULE);
}
static void render_root(uint8_t *fb){
    folio_ui_header(fb,&s_settings.status,"设置",false);folio_ui_icon(fb,FOLIO_ICON_MORE,600,108,36);
    ui_fill_round_rect(fb,(EpdRect){32,184,620,128},20,FOLIO_SURFACE);
    folio_ui_icon(fb,FOLIO_ICON_DEVICE,54,208,58);
    folio_ui_text(fb,126,209,34,"小纸 Pico",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,FOLIO_SURFACE);
    char info[96],battery[24];
    if(s_settings.status.battery_percent<0)snprintf(battery,sizeof(battery),"电量未知");else snprintf(battery,sizeof(battery),"%d%% 电量",s_settings.status.battery_percent);
    snprintf(info,sizeof(info),"%s · %s",battery,s_settings.status.sd_ready?"TF 卡可用":"TF 卡未就绪");
    folio_ui_text(fb,126,258,26,info,EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,FOLIO_SURFACE);
    folio_ui_chevron(fb,619,243,false);
    folio_ui_text(fb,40,350,26,"阅读体验",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    ui_fill_round_rect(fb,(EpdRect){32,398,620,212},20,FOLIO_SURFACE);
    group_row(fb,398,FOLIO_ICON_BOOK,"阅读偏好","字体与翻页",true);
    group_row(fb,504,FOLIO_ICON_REFRESH,"屏幕刷新","均衡",false);
    folio_ui_text(fb,40,650,26,"设备",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    ui_fill_round_rect(fb,(EpdRect){32,698,620,318},20,FOLIO_SURFACE);
    group_row(fb,698,FOLIO_ICON_WIFI,"Wi-Fi 与传书","按需开启",true);
    group_row(fb,804,FOLIO_ICON_SLEEP,"休眠与唤醒",s_settings.sleep==APP_SLEEP_LIGHT?"FAST":s_settings.sleep==APP_SLEEP_DEEP?"ECO":"关机",true);
    group_row(fb,910,FOLIO_ICON_LIBRARY,"字体与存储",NULL,false);
    folio_ui_text(fb,40,1050,28,"关于与帮助",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);folio_ui_chevron(fb,624,1063,false);
    folio_ui_tabs(fb,2);
}
static void render(app_ctx_t *ctx,uint8_t *fb){
    (void)ctx;
    if(s_settings.view==ROOT){render_root(fb);return;}
    const char *titles[]={"设置","阅读偏好","屏幕刷新","休眠与唤醒","字体与存储","关于与帮助"};
    folio_ui_header(fb,&s_settings.status,titles[s_settings.view],true);
    switch(s_settings.view){
    case READING:
        folio_ui_row(fb,214,"默认字体与排版","新书默认",0xFF);
        folio_ui_text(fb,40,298,24,"仅影响随后新打开的图书",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_row(fb,358,"默认阅读背景","编辑草稿",0xFF);
        folio_ui_text(fb,40,442,24,"已有图书保留各自设置",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,526,34,"翻页方式",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,590,530,26,"点按 + 滑动",EPD_DRAW_ALIGN_RIGHT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,586,24,"中间轻点始终打开工具",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,670,34,"页眉与页脚",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,590,674,26,"按本书排版",EPD_DRAW_ALIGN_RIGHT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,730,24,"与各本图书的排版一起保存",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_row(fb,790,"晃动翻页",s_settings.shake?"开启":"关闭",0xFF);
        folio_ui_text(fb,40,882,24,"实验功能，默认关闭",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        break;
    case REFRESH:
        folio_ui_text(fb,40,228,38,"均衡刷新",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,318,28,"优先保持灰阶与阅读清晰。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,382,28,"内容变化按区域刷新，必要时清理残影。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,446,28,"主动清残影会出现一次完整黑闪。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_button(fb,(EpdRect){40,598,604,96},"立即清残影",true);
        break;
    case SLEEP:
        folio_ui_row(fb,204,"睡眠模式",s_settings.sleep==APP_SLEEP_LIGHT?"FAST":s_settings.sleep==APP_SLEEP_DEEP?"ECO":"关机",0xFF);
        folio_ui_row(fb,350,"锁屏壁纸","自定义",0xFF);
        folio_ui_row(fb,496,"日期与时间","本机设置",0xFF);
        folio_ui_text(fb,40,676,28,"FAST 正常唤醒可保留当前内存。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,740,28,"ECO 断电前需要保存成功。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,804,28,"突然断电只恢复最后成功保存的内容。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        break;
    case STORAGE:
        folio_ui_row(fb,204,"阅读字体","内置 / TF",0xFF);
        folio_ui_row(fb,350,"TF 卡状态",s_settings.status.sd_ready?"可用":"未就绪",0xFF);
        folio_ui_row(fb,496,"内置存储","状态与恢复",0xFF);
        folio_ui_text(fb,40,686,28,"请先退出阅读和传书，再移除 TF 卡。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,750,28,"内置存储和统计需要保留可用空间。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        break;
    case HELP:
        folio_ui_text(fb,40,220,38,"小纸 Pico 阅读器",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,310,28,"本地 TXT / EPUB 阅读与离线统计",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,382,28,"触摸正文左右侧翻页，中间打开工具。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,446,28,"第二触摸键清残影，第三键打开菜单。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,510,28,"短按电源键锁屏；睡前先保存记录。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_row(fb,638,"日期与时间","校时",0xFF);
        folio_ui_row(fb,782,"设备信息","诊断",0xFF);
        break;
    default:break;
    }
    if(s_settings.format_error[0])folio_ui_text_fit(fb,40,1044,24,604,s_settings.format_error,FOLIO_INK,0xFF);
    if(s_settings.storage_error)folio_ui_text_fit(fb,40,1004,26,604,"设置存储不可用，修改可能无法保留。",FOLIO_SECONDARY,0xFF);
    folio_ui_button(fb,(EpdRect){40,1100,604,96},"返回设置",false);
}
static app_redraw_t on_gesture(app_ctx_t *ctx,const ui_gesture_event_t *e){
    if(e->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    int x=e->x0,y=e->y0;
    if(s_settings.view!=ROOT&&(ui_rect_hit((EpdRect){24,78,96,96},x,y)||ui_rect_hit((EpdRect){40,1100,604,96},x,y))){s_settings.view=ROOT;return APP_REDRAW_PAGE;}
    switch(s_settings.view){
    case ROOT:{
        int tab=folio_ui_tab_hit(x,y);
        if(tab==0){ctx->request_app=&app_folio_home;return APP_REDRAW_NONE;}
        if(tab==1){app_book_show_library(ctx);return APP_REDRAW_NONE;}
        if(ui_rect_hit((EpdRect){552,78,96,96},x,y)||ui_rect_hit((EpdRect){32,184,620,128},x,y)||ui_rect_hit((EpdRect){32,1012,620,96},x,y))s_settings.view=HELP;
        else if(ui_rect_hit((EpdRect){40,398,604,106},x,y))s_settings.view=READING;
        else if(ui_rect_hit((EpdRect){40,504,604,106},x,y))s_settings.view=REFRESH;
        else if(ui_rect_hit((EpdRect){40,698,604,106},x,y)){ctx->request_app=&app_transfer;return APP_REDRAW_NONE;}
        else if(ui_rect_hit((EpdRect){40,804,604,106},x,y))s_settings.view=SLEEP;
        else if(ui_rect_hit((EpdRect){40,910,604,100},x,y))s_settings.view=STORAGE;
        else return APP_REDRAW_NONE;
        return APP_REDRAW_PAGE;
    }
    case READING:
        if(ui_rect_hit((EpdRect){32,214,620,112},x,y)){
            if(!folio_format_begin_defaults(ctx)){snprintf(s_settings.format_error,sizeof(s_settings.format_error),"%s",folio_format_error());return APP_REDRAW_PAGE;}
        }else if(ui_rect_hit((EpdRect){32,358,620,112},x,y)){
            if(folio_format_begin_defaults(ctx))folio_format_field_begin(FORMAT_FIELD_BACKGROUND);
            else {snprintf(s_settings.format_error,sizeof(s_settings.format_error),"%s",folio_format_error());return APP_REDRAW_PAGE;}
        }else if(ui_rect_hit((EpdRect){32,790,620,112},x,y)){
            app_settings_set_book_shake(!s_settings.shake);sample();return APP_REDRAW_PAGE;
        }
        break;
    case REFRESH:if(ui_rect_hit((EpdRect){40,598,604,96},x,y))return APP_REDRAW_FULL;break;
    case SLEEP:
        if(ui_rect_hit((EpdRect){32,204,620,112},x,y))ctx->request_app=&app_folio_sleep;
        if(ui_rect_hit((EpdRect){32,350,620,112},x,y))ctx->request_app=&app_folio_wallpaper;
        if(ui_rect_hit((EpdRect){32,496,620,112},x,y))ctx->request_app=&app_folio_datetime;
        break;
    case STORAGE:
        if(ui_rect_hit((EpdRect){32,204,620,112},x,y)){
            if(folio_format_begin_defaults(ctx)&&folio_format_field_begin(FORMAT_FIELD_FONT)&&folio_format_enter_font_child(false))ctx->request_app=&app_folio_font_picker;
            else {snprintf(s_settings.format_error,sizeof(s_settings.format_error),"%s",folio_format_error());return APP_REDRAW_PAGE;}
        }
        if(ui_rect_hit((EpdRect){32,350,620,112},x,y))ctx->request_app=&app_sd;
        if(ui_rect_hit((EpdRect){32,496,620,112},x,y))ctx->request_app=&app_folio_storage;
        break;
    case HELP:
        if(ui_rect_hit((EpdRect){32,638,620,112},x,y))ctx->request_app=&app_folio_datetime;
        if(ui_rect_hit((EpdRect){32,782,620,112},x,y))ctx->request_app=&app_home;
        break;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t *ctx,int key){
    if(key==UI_KEY_1){if(s_settings.view!=ROOT){s_settings.view=ROOT;return APP_REDRAW_PAGE;}ctx->request_app=&app_folio_home;}
    if(key==UI_KEY_2)return APP_REDRAW_FULL;
    if(key==UI_KEY_3)ctx->request_menu=true;
    return APP_REDRAW_NONE;
}
static int settings_metrics_scenario(void) {return FOLIO_METRICS_SETTINGS;}
const app_desc_t app_folio_settings={.metrics_scenario=settings_metrics_scenario,.title="设置",.detail="阅读、休眠与设备",.owns_keys=true,.owns_footer=true,
    .on_enter=on_enter,.render=render,.on_gesture=on_gesture,.on_key=on_key};
