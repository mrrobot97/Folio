/* SPDX-License-Identifier: Apache-2.0
 * TF 壁纸选择/预览/应用，预览不改变已应用选择。
 * TF wallpaper selection, preview and apply; preview never changes the active choice.
 */
#include "app.h"
#include "folio_ui.h"
#include "wallpaper.h"
#include "settings.h"
#include "ui_kit.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    folio_status_t status;
    char names[24][128], selected[160];
    int count, page;
    bool preview, more;
    uint8_t* pixels;
    const char* message;
} s_wall;
extern const uint8_t lock_4bpp_bin_start[] asm("_binary_lock_4bpp_bin_start");
static void scan(void) {
    s_wall.count = s_wall.page = 0; s_wall.more = false;
    DIR* d = opendir("/sdcard/wallpapers");
    s_wall.message = d ? "选择图片后预览，确认应用才生效" : "请在 TF 卡 wallpapers 目录放入壁纸";
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        char path[160];
        int len = snprintf(path,sizeof(path),"/sdcard/wallpapers/%s",e->d_name);
        if (len < 0 || len >= (int)sizeof(path) || strlen(e->d_name) >= sizeof(s_wall.names[0]) || !folio_wallpaper_path_valid(path)) continue;
        if (s_wall.count == 24) { s_wall.more = true; break; }
        strcpy(s_wall.names[s_wall.count++],e->d_name);
    }
    closedir(d);
}
static void enter(app_ctx_t* ctx) {
    (void)ctx; free(s_wall.pixels); s_wall.pixels = NULL; s_wall.preview = false;
    folio_ui_sample_status(&s_wall.status); scan();
}
static void leave(app_ctx_t* ctx) { (void)ctx; free(s_wall.pixels); s_wall.pixels = NULL; }
static void lost(app_ctx_t* ctx) { leave(ctx); s_wall.preview = false; s_wall.count = 0; s_wall.message = "TF 卡已移除，已应用选择保留"; }
static void render(app_ctx_t* ctx, uint8_t* fb) {
    (void)ctx;
    if (s_wall.preview) {
        ui_draw_full_image(fb,s_wall.pixels ? s_wall.pixels : lock_4bpp_bin_start);
        epd_fill_rect((EpdRect){0,996,684,220},0xFF,fb);
        folio_ui_text_fit(fb,40,1014,26,604,s_wall.message,FOLIO_SECONDARY,0xFF);
        folio_ui_button(fb,(EpdRect){40,1088,284,96},"返回选择",false);
        folio_ui_button(fb,(EpdRect){360,1088,284,96},"应用壁纸",true);
        return;
    }
    folio_ui_header(fb,&s_wall.status,"休眠壁纸",true);
    folio_ui_text_fit(fb,40,190,26,604,s_wall.message,FOLIO_SECONDARY,0xFF);
    folio_ui_row(fb,254,"内置壁纸",app_settings_wallpaper_path()[0] ? "" : "已应用",0xFF);
    for (int i = 0; i < 5 && s_wall.page*5+i < s_wall.count; ++i) {
        int n = s_wall.page*5+i; char title[128];
        snprintf(title,sizeof(title),"%s",s_wall.names[n]); title[strlen(title)-4] = 0;
        folio_ui_row(fb,380+i*106,title,"预览",0xFF);
    }
    folio_ui_button(fb,(EpdRect){40,978,180,96},"上一页",false);
    folio_ui_button(fb,(EpdRect){252,978,180,96},"重新读取",false);
    folio_ui_button(fb,(EpdRect){464,978,180,96},"下一页",false);
    folio_ui_text_fit(fb,40,1110,24,604,s_wall.more ? "当前显示前 24 张；请整理 TF 壁纸目录。" : "移除 TF 后无法重新加载时使用内置壁纸。",FOLIO_SECONDARY,0xFF);
}
static app_redraw_t gesture(app_ctx_t* ctx,const ui_gesture_event_t* e) {
    if (e->type != UI_GESTURE_TAP) return APP_REDRAW_NONE;
    if (s_wall.preview) {
        if (ui_rect_hit((EpdRect){40,1088,284,96},e->x0,e->y0)) { s_wall.preview = false; return APP_REDRAW_PAGE; }
        if (ui_rect_hit((EpdRect){360,1088,284,96},e->x0,e->y0)) {
            s_wall.message = app_settings_set_wallpaper_path(s_wall.selected) ? "壁纸已应用" : "保存失败，之前的壁纸保持不变";
            return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if (ui_rect_hit((EpdRect){0,78,110,96},e->x0,e->y0)) { ctx->request_return = true; return APP_REDRAW_NONE; }
    if (ui_rect_hit((EpdRect){40,254,604,106},e->x0,e->y0)) {
        free(s_wall.pixels); s_wall.pixels = NULL; s_wall.selected[0] = 0; s_wall.preview = true;
        s_wall.message = "内置壁纸预览"; return APP_REDRAW_PAGE;
    }
    for (int i = 0; i < 5 && s_wall.page*5+i < s_wall.count; ++i) {
        if (!ui_rect_hit((EpdRect){40,380+i*106,604,106},e->x0,e->y0)) continue;
        char path[160]; snprintf(path,sizeof(path),"/sdcard/wallpapers/%s",s_wall.names[s_wall.page*5+i]);
        uint8_t* pixels = NULL;
        if (!folio_wallpaper_read(path,&pixels)) { s_wall.message = "图片读取或校验失败，已应用壁纸不变"; return APP_REDRAW_PAGE; }
        free(s_wall.pixels); s_wall.pixels = pixels; strcpy(s_wall.selected,path); s_wall.preview = true;
        s_wall.message = "预览 · 尚未应用"; return APP_REDRAW_PAGE;
    }
    if (ui_rect_hit((EpdRect){40,978,180,96},e->x0,e->y0) && s_wall.page > 0) --s_wall.page;
    else if (ui_rect_hit((EpdRect){464,978,180,96},e->x0,e->y0) && (s_wall.page+1)*5 < s_wall.count) ++s_wall.page;
    else if (ui_rect_hit((EpdRect){252,978,180,96},e->x0,e->y0)) scan();
    else return APP_REDRAW_NONE;
    return APP_REDRAW_PAGE;
}
static app_redraw_t key(app_ctx_t* ctx,int key) {
    if (key == UI_KEY_1) {
        if (s_wall.preview) { s_wall.preview = false; return APP_REDRAW_PAGE; }
        ctx->request_return = true;
    }
    if (key == UI_KEY_2) return APP_REDRAW_FULL;
    if (key == UI_KEY_3) ctx->request_menu = true;
    return APP_REDRAW_NONE;
}
const app_desc_t app_folio_wallpaper = {.title="休眠壁纸",.detail="TF 图片预览与应用",.owns_keys=true,.owns_footer=true,
    .on_enter=enter,.on_exit=leave,.on_media_lost=lost,.render=render,.on_gesture=gesture,.on_key=key};
