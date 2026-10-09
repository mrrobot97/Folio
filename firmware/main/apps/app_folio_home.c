/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：H01/H02 内容首页，真实续读、书架、导入与本地统计入口。
 * English: H01/H02 content home with real resume, library, transfer and local statistics.
 * 冻结：进入书架不隐式续读；未校时不虚构今日数据；render 不做 I/O。
 * Frozen: Library is distinct from resume; unknown dates never invent today; render is I/O-free.
 */
#include "app.h"
#include "folio_metrics.h"
#include "folio_ui.h"
#include "folio_navigation.h"
#include "reading_service.h"
#include "reader_runtime.h"
#include "format_service.h"
#include "book_store.h"
#include "ui_kit.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

extern const app_desc_t app_transfer;
#define HOME_BOOKS 3
static struct {
    folio_status_t status;
    folio_reading_summary_t summary;
    int64_t date_check_ms;
    bool resume, scan_ok;
    char title[128], path[288];
    char action_error[128];
    unsigned percent;
    struct { char title[128], path[288]; bool epub; } books[HOME_BOOKS];
    int count;
} s_home;

static void scan_books(void) {
    book_store_root_t roots[2]; int count = 0;
    s_home.count = 0; s_home.scan_ok = false;
    if (book_store_roots(roots, &count) != ESP_OK) return;
    s_home.scan_ok = true;
    for (int r = 0; r < count && s_home.count < HOME_BOOKS; r++) {
        DIR *dir = opendir(roots[r].path);
        if (!dir) { s_home.scan_ok = false; continue; }
        struct dirent *entry;
        while (s_home.count < HOME_BOOKS && (entry = readdir(dir))) {
            const char *ext = strrchr(entry->d_name, '.');
            if (!ext || (strcasecmp(ext, ".txt") && strcasecmp(ext, ".epub"))) continue;
            char path[288];
            int n = snprintf(path, sizeof(path), "%s/%s", roots[r].path, entry->d_name);
            struct stat st;
            if (n < 0 || n >= (int)sizeof(path) || stat(path, &st) || !S_ISREG(st.st_mode)) continue;
            int i = s_home.count++;
            snprintf(s_home.books[i].path, sizeof(s_home.books[i].path), "%s", path);
            size_t len = (size_t)(ext - entry->d_name);
            if (len >= sizeof(s_home.books[i].title)) {
                len = sizeof(s_home.books[i].title) - 1;
                while (len && ((unsigned char)entry->d_name[len] & 0xC0) == 0x80) len--;
            }
            memcpy(s_home.books[i].title, entry->d_name, len); s_home.books[i].title[len] = 0;
            s_home.books[i].epub = !strcasecmp(ext, ".epub");
        }
        closedir(dir);
    }
}
static void on_enter(app_ctx_t *ctx) {
    s_home.action_error[0] = '\0';
    folio_ui_sample_status(&s_home.status);
    s_home.summary = folio_reading_summary_at(ctx->now_ms);
    s_home.date_check_ms = ctx->now_ms;
    s_home.resume = app_book_recent(s_home.title, sizeof(s_home.title), s_home.path, sizeof(s_home.path), &s_home.percent);
    scan_books();
}
static app_redraw_t on_tick(app_ctx_t *ctx) {
    if (ctx->now_ms >= s_home.date_check_ms && ctx->now_ms - s_home.date_check_ms < 1000) return APP_REDRAW_NONE;
    s_home.date_check_ms = ctx->now_ms;
    folio_reading_summary_t current = folio_reading_summary_at(ctx->now_ms);
    if (current.today_day == s_home.summary.today_day) return APP_REDRAW_NONE;
    s_home.summary = current;
    folio_ui_sample_status(&s_home.status);
    return APP_REDRAW_PAGE;
}
static void cover(uint8_t *fb, EpdRect r, const char *title) {
    ui_fill_round_rect(fb, r, 4, 0xDD);
    epd_draw_line(r.x+8,r.y+4,r.x+8,r.y+r.height-4,0xAA,fb);
    folio_ui_text_fit(fb,r.x+20,r.y+24,26,r.width-32,title,FOLIO_INK,0xDD);
    folio_ui_icon(fb,FOLIO_ICON_BOOK,r.x+r.width/2-20,r.y+r.height-70,40);
}
static void render(app_ctx_t *ctx, uint8_t *fb) {
    (void)ctx;
    folio_ui_header(fb, &s_home.status, "小纸 Pico", false);
    if (s_home.action_error[0]) folio_ui_text_fit(fb,40,148,26,604,s_home.action_error,FOLIO_SECONDARY,0xFF);
    folio_ui_icon(fb,FOLIO_ICON_SEARCH,598,104,36);
    ui_fill_round_rect(fb,(EpdRect){32,184,620,348},24,FOLIO_SURFACE);
    cover(fb,(EpdRect){64,216,132,194},s_home.resume?s_home.title:"一本好书");
    folio_ui_text(fb,228,216,26,s_home.resume?"继续上次的阅读":"给自己一点阅读时间",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,FOLIO_SURFACE);
    folio_ui_text_fit(fb,228,266,42,372,s_home.resume?s_home.title:"从第一本开始",FOLIO_INK,FOLIO_SURFACE);
    folio_ui_text(fb,228,334,26,s_home.resume?"上次保存的位置":"TXT / EPUB · 随身阅读",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,FOLIO_SURFACE);
    if(s_home.resume){
        epd_fill_rect((EpdRect){228,386,372,4},0xBB,fb);
        epd_fill_rect((EpdRect){228,386,(int)(372*s_home.percent/100),4},FOLIO_INK,fb);
        char progress[32];snprintf(progress,sizeof(progress),"已读 %u%%",s_home.percent);
        folio_ui_text(fb,228,402,26,progress,EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,FOLIO_SURFACE);
    }
    char duration[48], today[96];
    folio_ui_duration(s_home.summary.today_ms,duration,sizeof(duration));
    if(s_home.summary.today_day)snprintf(today,sizeof(today),"今日总阅读 %s ›",duration);
    else snprintf(today,sizeof(today),"未校时 · 查看阅读统计 ›");
    folio_ui_text_fit(fb,64,462,26,340,today,FOLIO_SECONDARY,FOLIO_SURFACE);
    folio_ui_button(fb,(EpdRect){416,426,204,96},s_home.resume?"继续阅读":"开始传书",true);
    static const char *labels[]={"传书","统计","字体","清残影"};
    static const folio_icon_t icons[]={FOLIO_ICON_WIFI,FOLIO_ICON_LIBRARY,FOLIO_ICON_SETTINGS,FOLIO_ICON_REFRESH};
    for(int i=0;i<4;i++){int x=110+156*i;folio_ui_icon(fb,icons[i],x-20,580,40);
        folio_ui_text(fb,x,638,28,labels[i],EPD_DRAW_ALIGN_CENTER,FOLIO_INK,0xFF);}
    if(s_home.count){
        folio_ui_text(fb,40,730,34,"本地图书",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,644,734,28,"全部",EPD_DRAW_ALIGN_RIGHT,FOLIO_SECONDARY,0xFF);
        for(int i=0;i<s_home.count;i++){
            int x=40+i*220;cover(fb,(EpdRect){x,808,164,180},s_home.books[i].title);
            folio_ui_text_fit(fb,x,1006,28,164,s_home.books[i].title,FOLIO_INK,0xFF);
            folio_ui_text(fb,x,1045,24,s_home.books[i].epub?"EPUB":"TXT",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        }
    }else{
        folio_ui_text(fb,40,754,34,s_home.scan_ok?"准备好，就开始":"书架暂时不可用",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,818,28,s_home.scan_ok?"传入图书后，它们会出现在这里。":"检查存储后，重新打开书架。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,932,30,"第一次使用",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,978,26,"了解翻页、按键与字体",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_chevron(fb,623,968,false);
    }
    folio_ui_tabs(fb,0);
}
static app_redraw_t on_gesture(app_ctx_t *ctx,const ui_gesture_event_t *e){
    if(e->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    int x=e->x0,y=e->y0,tab=folio_ui_tab_hit(x,y);
    if(tab==1){app_book_show_library(ctx);return APP_REDRAW_NONE;}
    if(tab==2){ctx->request_app=&app_folio_settings;return APP_REDRAW_NONE;}
    if(ui_rect_hit((EpdRect){48,426,344,96},x,y)){folio_stats_open(ctx);return APP_REDRAW_NONE;}
    if(ui_rect_hit((EpdRect){32,184,620,236},x,y)||ui_rect_hit((EpdRect){416,426,216,96},x,y)){
        if(s_home.resume)app_book_resume_last(ctx);else ctx->request_app=&app_transfer;
        return APP_REDRAW_NONE;
    }
    if(ui_rect_hit((EpdRect){552,78,96,96},x,y)){app_book_show_search(ctx);return APP_REDRAW_NONE;}
    if(ui_rect_hit((EpdRect){540,712,112,96},x,y)){app_book_show_library(ctx);return APP_REDRAW_NONE;}
    for(int i=0;i<4;i++)if(ui_rect_hit((EpdRect){42+156*i,562,136,124},x,y)){
        if(i==0)ctx->request_app=&app_transfer;
        if(i==1)folio_stats_open(ctx);
        if(i==2){
            if(folio_format_begin_defaults(ctx))ctx->request_app=&app_folio_format;
            else {snprintf(s_home.action_error,sizeof(s_home.action_error),"%s",folio_format_error());return APP_REDRAW_PAGE;}
        }
        if(i==3)return APP_REDRAW_FULL;
        return APP_REDRAW_NONE;
    }
    for(int i=0;i<s_home.count;i++)if(ui_rect_hit((EpdRect){32+i*220,808,180,280},x,y)){
        app_book_open_path(ctx,s_home.books[i].path);return APP_REDRAW_NONE;
    }
    if(!s_home.count&&ui_rect_hit((EpdRect){32,916,620,116},x,y))ctx->request_app=&app_folio_settings;
    return APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t *ctx,int key){
    if(key==UI_KEY_1)app_book_show_library(ctx);
    if(key==UI_KEY_2)return APP_REDRAW_FULL;
    if(key==UI_KEY_3)ctx->request_menu=true;
    return APP_REDRAW_NONE;
}
static int folio_page_metric_scene(void) { return FOLIO_METRICS_HOME; }
const app_desc_t app_folio_home={ .metrics_scenario=folio_page_metric_scene,.title="主页",.detail="阅读与本地统计",.owns_keys=true,.owns_footer=true,
    .on_enter=on_enter,.render=render,.on_gesture=on_gesture,.on_key=on_key,.on_tick=on_tick};
