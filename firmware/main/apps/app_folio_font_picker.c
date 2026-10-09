/* SPDX-License-Identifier: Apache-2.0
 * R10/S05：真实字体资源与独立候选预览，始终归属同一排版会话。
 * R10/S05: real font resources and isolated candidate preview owned by one format session.
 * 冻结：不自动替换缺失字体；上传只由明确操作启动；render 不扫描或打开文件。
 * Frozen: never silently replace missing fonts or auto-start upload; render never scans or opens files.
 */
#include "app.h"
#include "folio_metrics.h"
#include "folio_navigation.h"
#include "format_service.h"
#include "format_ui.h"
#include "ttf_font.h"
#include "read_pico_sd.h"
#include "transfer_navigation.h"
#include "ui_kit.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include <stdio.h>
#include <string.h>

typedef enum { PICKER_LIST, PICKER_COVERAGE, PICKER_ERROR, PICKER_MISSING } picker_view_t;
static struct {folio_status_t status;folio_format_preview_t preview;ttf_font_item_t items[TTF_FONT_MAX];
    int count,page;picker_view_t view;bool card_ok,session_ok;char error[192];} s_picker;
static const char *item_path(int index) {return index==s_picker.count?TTF_FONT_BUILTIN:s_picker.items[index].path;}
static void error_text(const char *fallback) {
    const char *error=folio_format_error();
    snprintf(s_picker.error,sizeof(s_picker.error),"%s",error&&error[0]?error:fallback);
}
static void scan(void) {
    read_pico_sd_info_t info={0};s_picker.card_ok=read_pico_sd_get_info(&info)==ESP_OK&&info.mounted;
    s_picker.count=0;
    if(s_picker.card_ok){
        int count=ttf_font_scan();if(count>TTF_FONT_MAX)count=TTF_FONT_MAX;
        for(int i=0;i<count;i++){const ttf_font_item_t *item=ttf_font_item(i);if(item)s_picker.items[s_picker.count++]=*item;}
    }
    int last=s_picker.count/3;if(s_picker.page>last)s_picker.page=last;
}
static bool prepare_current(void) {
    const folio_format_snapshot_t *s=folio_format_snapshot();
    return s&&folio_format_ui_prepare(&s->candidate,&s_picker.preview);
}
static void on_enter(app_ctx_t *ctx) {
    (void)ctx;folio_ui_sample_status(&s_picker.status);
    folio_format_set_owner_visible(true);
    const folio_format_snapshot_t *s=folio_format_snapshot();
    s_picker.session_ok=s&&s->lifecycle==FORMAT_EDITING&&s->field==FORMAT_FIELD_FONT;
    if(s)snprintf(s_picker.status.clock,sizeof(s_picker.status.clock),"%s",s->recovered_checkpoint?"已恢复未应用草稿":s->scope==FORMAT_DEFAULTS?"新书默认排版草稿":"本书排版草稿");
    s_picker.view=PICKER_LIST;s_picker.error[0]=0;
    if(!s_picker.session_ok){s_picker.view=PICKER_ERROR;error_text("排版会话暂不可用，原设置保持不变。");return;}
    if(s->route==FORMAT_ROUTE_FONT_TRANSFER&&!folio_format_leave_font_child(true)){
        s_picker.view=PICKER_ERROR;error_text("字体传输尚未停止，草稿仍保留。");return;
    }
    s_picker.page=s->scroll/3;scan();
    if(!prepare_current())s_picker.view=PICKER_MISSING;
}
static void render_list(uint8_t *fb,const folio_format_snapshot_t *s) {
    folio_ui_header(fb,&s_picker.status,"选择阅读字体",true);
    for(int row=0;row<3;row++){
        int index=s_picker.page*3+row;if(index>s_picker.count)break;
        const char *path=item_path(index);
        bool builtin=index==s_picker.count;
        bool selected=builtin?(!s->candidate.font.path[0]||strcmp(s->candidate.font.path,TTF_FONT_BUILTIN)==0):strcmp(path,s->candidate.font.path)==0;
        const char *name=builtin?"内置界面字体":s_picker.items[index].name;
        folio_format_ui_radio(fb,(EpdRect){32,216+row*128,620,112},name,builtin?"界面子集 · 正文可能缺字":"TF 卡字体 · 全文覆盖尚未验证",selected);
    }
    int pages=s_picker.count/3+1;
    if(pages>1){
        folio_ui_button(fb,(EpdRect){40,584,180,96},"上一页",false);
        folio_ui_button(fb,(EpdRect){464,584,180,96},"下一页",false);
        char page[24];snprintf(page,sizeof(page),"%d / %d",s_picker.page+1,pages);
        folio_ui_text(fb,342,616,26,page,EPD_DRAW_ALIGN_CENTER,FOLIO_SECONDARY,0xFF);
    }
    EpdRect preview=pages>1?(EpdRect){32,696,620,160}:(EpdRect){32,644,620,198};
    folio_format_ui_preview(fb,preview,&s->candidate,&s_picker.preview,"山风轻轻翻过这一页。","Reading, at your pace.",false,false);
    char status[128];
    if(!s_picker.card_ok)snprintf(status,sizeof(status),"TF 卡未就绪；原候选不会自动替换。");
    else if(!s_picker.count)snprintf(status,sizeof(status),"未找到卡上 TTF；可上传后重新检查。");
    else if(folio_format_weight_number(&s->candidate)<s_picker.preview.weight_min||folio_format_weight_number(&s->candidate)>s_picker.preview.weight_max)snprintf(status,sizeof(status),"此字体不支持草稿字重，请返回后调整。");
    else if(!s_picker.preview.sample_complete)snprintf(status,sizeof(status),"样例缺字 U+%04lX；不代表完整正文覆盖。",(unsigned long)s_picker.preview.first_missing);
    else snprintf(status,sizeof(status),"样例可显示，完整正文覆盖尚未验证。");
    folio_ui_text_fit(fb,40,866,24,604,status,FOLIO_SECONDARY,0xFF);
    folio_ui_button(fb,(EpdRect){40,914,604,96},"上传其他字体",false);
    folio_format_ui_footer(fb,"保留到草稿");
}
static void render(app_ctx_t *ctx,uint8_t *fb) {
    (void)ctx;const folio_format_snapshot_t *s=folio_format_snapshot();
    if(s_picker.view==PICKER_LIST&&s_picker.session_ok&&s){render_list(fb,s);return;}
    if(s_picker.view==PICKER_COVERAGE){
        folio_ui_header(fb,&s_picker.status,"内置字体可能缺字",true);
        folio_ui_text(fb,40,354,30,"内置界面字体在设备中可用，但只覆盖",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,402,30,"界面和演示文字。",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,498,30,"外部图书可能出现缺字，这与卡上字体",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,546,30,"文件丢失不同。确认仅保留字段候选。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_button(fb,(EpdRect){40,958,604,96},"返回选择",false);
        folio_ui_button(fb,(EpdRect){40,1076,604,96},"保留为字体候选",true);return;
    }
    bool missing=s_picker.view==PICKER_MISSING;
    folio_ui_header(fb,&s_picker.status,missing?"草稿需要原来的字体":"字体未能打开",true);
    folio_ui_text(fb,40,354,30,missing?"候选字体暂不可用，草稿仍然保留。":"字体格式不支持、文件损坏或存储异常。",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
    folio_ui_text(fb,40,450,28,"不会自动应用其他字体，也不改变正文。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    if(s_picker.error[0])folio_ui_text_fit(fb,40,546,26,604,s_picker.error,FOLIO_SECONDARY,0xFF);
    folio_ui_button(fb,(EpdRect){40,958,604,96},missing?"重新检查资源":"选择其他字体",false);
    folio_ui_button(fb,(EpdRect){40,1076,604,96},missing?"返回字体列表":"返回排版草稿",false);
}
static app_redraw_t choose(int index) {
    if(index<0||index>s_picker.count)return APP_REDRAW_NONE;
    folio_format_font_t font={0};ttf_font_identity_t identity={0};
    if(ttf_font_identity_read(item_path(index),&identity)!=ESP_OK){
        snprintf(s_picker.error,sizeof(s_picker.error),"所选字体资源已变化或无法读取。");s_picker.view=PICKER_ERROR;return APP_REDRAW_PAGE;
    }
    snprintf(font.path,sizeof(font.path),"%s",identity.path);font.file_size=identity.file_size;font.mtime=identity.mtime;font.header_crc=identity.header_crc;
    const folio_format_snapshot_t *s=folio_format_snapshot();folio_format_values_t candidate=s->candidate;candidate.font=font;
    folio_format_preview_t preview;
    if(!folio_format_ui_prepare(&candidate,&preview)){
        snprintf(s_picker.error,sizeof(s_picker.error),"预览未能打开。当前字段候选仍保持原值。");s_picker.view=PICKER_ERROR;return APP_REDRAW_PAGE;
    }
    if(!folio_format_set_font(&font)){error_text("字体候选未能保留。");s_picker.view=PICKER_ERROR;return APP_REDRAW_PAGE;}
    s_picker.preview=preview;s_picker.view=PICKER_LIST;
    if(!folio_format_set_view((uint8_t)index,0,(uint8_t)(s_picker.page*3)))error_text("选择位置暂存失败，候选仍在内存。");
    return APP_REDRAW_PAGE;
}
static app_redraw_t finish(app_ctx_t *ctx,bool keep) {
    if(!s_picker.session_ok){ctx->request_return=true;return APP_REDRAW_NONE;}
    if(!folio_format_leave_font_child(true)||(keep?!folio_format_field_keep():!folio_format_field_cancel())){
        error_text("未能返回排版草稿，当前候选仍保留。");s_picker.view=PICKER_ERROR;return APP_REDRAW_PAGE;
    }
    ctx->request_app=&app_folio_format;return APP_REDRAW_NONE;
}
static app_redraw_t on_gesture(app_ctx_t *ctx,const ui_gesture_event_t *e) {
    if(e->type==UI_GESTURE_SWIPE_U||e->type==UI_GESTURE_SWIPE_D){
        if(s_picker.view!=PICKER_LIST)return APP_REDRAW_NONE;
        int next=s_picker.page+(e->type==UI_GESTURE_SWIPE_U?1:-1);
        if(next<0||next>s_picker.count/3)return APP_REDRAW_NONE;
        s_picker.page=next;folio_format_set_view(0,0,(uint8_t)(next*3));return APP_REDRAW_PAGE;
    }
    if(e->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    int x=e->x0,y=e->y0;
    if(ui_rect_hit((EpdRect){24,78,96,96},x,y)){
        if(s_picker.view!=PICKER_LIST){s_picker.view=PICKER_LIST;prepare_current();return APP_REDRAW_PAGE;}
        return finish(ctx,false);
    }
    if(s_picker.view==PICKER_COVERAGE){
        if(ui_rect_hit((EpdRect){40,958,604,96},x,y)){s_picker.view=PICKER_LIST;return APP_REDRAW_PAGE;}
        if(ui_rect_hit((EpdRect){40,1076,604,96},x,y))return choose(s_picker.count);
        return APP_REDRAW_NONE;
    }
    if(s_picker.view==PICKER_MISSING||s_picker.view==PICKER_ERROR){
        if(ui_rect_hit((EpdRect){40,958,604,96},x,y)){scan();prepare_current();s_picker.view=PICKER_LIST;return APP_REDRAW_PAGE;}
        if(ui_rect_hit((EpdRect){40,1076,604,96},x,y)){
            if(s_picker.view==PICKER_ERROR)return finish(ctx,false);
            s_picker.view=PICKER_LIST;return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if(ui_rect_hit((EpdRect){40,1100,290,96},x,y))return finish(ctx,false);
    if(ui_rect_hit((EpdRect){346,1100,298,96},x,y))return finish(ctx,true);
    for(int row=0;row<3;row++)if(ui_rect_hit((EpdRect){32,216+row*128,620,112},x,y)){
        int index=s_picker.page*3+row;if(index>s_picker.count)return APP_REDRAW_NONE;
        if(index==s_picker.count){s_picker.view=PICKER_COVERAGE;return APP_REDRAW_PAGE;}
        return choose(index);
    }
    if(s_picker.count>=3){
        if(ui_rect_hit((EpdRect){40,584,180,96},x,y)&&s_picker.page>0){s_picker.page--;folio_format_set_view(0,0,(uint8_t)(s_picker.page*3));return APP_REDRAW_PAGE;}
        if(ui_rect_hit((EpdRect){464,584,180,96},x,y)&&s_picker.page<s_picker.count/3){s_picker.page++;folio_format_set_view(0,0,(uint8_t)(s_picker.page*3));return APP_REDRAW_PAGE;}
    }
    if(ui_rect_hit((EpdRect){40,914,604,96},x,y)){
        if(!folio_format_enter_font_child(true)){error_text("字体子流程未能安全暂存。");s_picker.view=PICKER_ERROR;return APP_REDRAW_PAGE;}
        app_transfer_open_font_child(ctx,&app_folio_font_picker);return APP_REDRAW_NONE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t *ctx,int key) {
    if(key==UI_KEY_1)return finish(ctx,false);
    if(key==UI_KEY_2)return APP_REDRAW_FULL;
    if(key==UI_KEY_3){folio_format_request_menu(ctx);return APP_REDRAW_NONE;}
    return APP_REDRAW_NONE;
}
static void on_media_lost(app_ctx_t *ctx) {
    folio_format_media_lost(ctx);s_picker.card_ok=false;s_picker.preview.ready=false;s_picker.view=PICKER_MISSING;
    snprintf(s_picker.error,sizeof(s_picker.error),"TF 卡已不可用，当前草稿与原设置均保留。");
}
static void font_picker_on_exit(app_ctx_t *ctx) {
    (void)ctx;folio_format_set_owner_visible(false);
}
static int font_picker_metrics_scenario(void) {return FOLIO_METRICS_FORMAT;}
const app_desc_t app_folio_font_picker={.metrics_scenario=font_picker_metrics_scenario,.title="选择草稿字体",.detail="真实资源与候选预览",.owns_keys=true,.owns_footer=true,.manages_font=true,
    .on_enter=on_enter,.on_exit=font_picker_on_exit,.on_media_lost=on_media_lost,.render=render,.on_gesture=on_gesture,.on_key=on_key};
