/* SPDX-License-Identifier: Apache-2.0
 * v0.4.2 R04/R14/R15/R11：BOOK/DEFAULTS 共用事务 UI。
 * v0.4.2 R04/R14/R15/R11: shared transactional UI for BOOK/DEFAULTS.
 * 冻结：字段只写候选，根提交 ACK 后生效；render 不打开资源或持久化。
 * Frozen: fields edit candidates; activation requires a root ACK; render opens/persists nothing.
 */
#include "app.h"
#include "folio_metrics.h"
#include "folio_navigation.h"
#include "format_service.h"
#include "format_ui.h"
#include "ui_kit.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "ttf_font.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include <stdio.h>
#include <string.h>

typedef enum { FORMAT_NORMAL, FORMAT_DISCARD_CONFIRM, FORMAT_COMMIT_ERROR, FORMAT_DARK_CONFIRM, FORMAT_SAVING } format_overlay_t;
static struct {folio_status_t status;folio_format_preview_t preview;format_overlay_t overlay;bool menu_after_discard;bool unavailable,discard_failed;char error[192];} s_format;
static const folio_format_values_t *shown_values(void) {
    const folio_format_snapshot_t *s=folio_format_snapshot();
    return s?s->field==FORMAT_FIELD_NONE?&s->root:&s->candidate:NULL;
}
static void remember_error(void) {
    const char *error=folio_format_error();
    snprintf(s_format.error,sizeof(s_format.error),"%s",error&&error[0]?error:"操作未完成，草稿仍然保留。");
}
static void prepare_preview(void) {
    const folio_format_values_t *v=shown_values();
    folio_format_ui_prepare(v,&s_format.preview);
}
static void on_enter(app_ctx_t *ctx) {
    (void)ctx;folio_ui_sample_status(&s_format.status);
    folio_format_set_owner_visible(true);
    const folio_format_snapshot_t *s=folio_format_snapshot();
    s_format.unavailable=!s||s->lifecycle!=FORMAT_EDITING;
    s_format.error[0]=0;
    if(folio_format_error()[0])remember_error();
    s_format.overlay=s&&s->requested_leave?FORMAT_DISCARD_CONFIRM:FORMAT_NORMAL;
    if(!s_format.unavailable)prepare_preview();
    else remember_error();
}
static void font_row_label(const char *path,char *out,size_t cap) {
    folio_format_ui_font_name(path,out,cap);
    ttf_font_scope_t scope;
    if(ttf_font_begin(TTF_ROLE_UI,&scope)!=ESP_OK)return;
    int weight=ttf_get_weight();ttf_set_weight(400);
    int px=ttf_font_pixel_height_for_em(26);
    size_t n=strlen(out);
    if(px>0&&ttf_text_width_px(px,out)>270){
        while(n){
            n--;while(n&&((unsigned char)out[n]&0xC0)==0x80)n--;
            out[n]=0;
            if(n+4<cap){memcpy(out+n,"…",4);if(ttf_text_width_px(px,out)<=270)break;out[n]=0;}
        }
    }
    ttf_set_weight(weight);ttf_font_end(&scope);
}
static void render_root(uint8_t *fb,const folio_format_snapshot_t *s) {
    bool defaults=s->scope==FORMAT_DEFAULTS;
    folio_ui_header(fb,&s_format.status,defaults?"新书默认排版":"本书字体与排版",true);
    folio_format_ui_preview(fb,(EpdRect){32,194,620,198},&s->root,&s_format.preview,
        "山风轻轻翻过这一页。","慢一点，也能抵达远方。",true,false);
    char value[96],font[96];font_row_label(s->root.font.path,font,sizeof(font));
    folio_ui_row(fb,422,"字体",font,0xFF);
    snprintf(value,sizeof(value),"%u / %s",s->root.px,folio_format_ui_weight(s->root.weight));
    folio_ui_row(fb,544,"字号与字重",value,0xFF);
    static const char *spacing[]={"紧凑","标准","宽松"};
    static const char *margin[]={"窄","标准","宽"};
    snprintf(value,sizeof(value),"%s / %s",spacing[s->root.spacing],margin[s->root.margin]);
    folio_ui_row(fb,666,"行距与页边距",value,0xFF);
    folio_ui_row(fb,788,"阅读背景",folio_format_ui_background(s->root.background),0xFF);
    folio_ui_text_fit(fb,40,934,26,604,s->root_dirty?"草稿未应用，正文仍使用原来的排版。":defaults?"仅设置新书默认，不改变已读图书。":"各子页先保留草稿，最后在这里统一应用。",FOLIO_SECONDARY,0xFF);
    int requested_weight=folio_format_weight_number(&s->root);
    if(s_format.preview.ready&&(requested_weight<s_format.preview.weight_min||requested_weight>s_format.preview.weight_max))
        folio_ui_text_fit(fb,40,996,24,604,"当前字体不支持此字重，请调整字重。",FOLIO_SECONDARY,0xFF);
    else if(s_format.preview.builtin||!s_format.preview.sample_complete)
        folio_ui_text_fit(fb,40,996,24,604,s_format.preview.builtin?"内置字体为界面子集，正文可能缺字。":"样例存在缺字，请检查候选字体。",FOLIO_SECONDARY,0xFF);
    folio_format_ui_footer(fb,defaults?"保存默认":"应用更改");
}
static void size_step(uint8_t *fb,EpdRect rect,const char *label,bool enabled) {
    EpdRect visible={rect.x,rect.y+12,rect.width,rect.height-24};
    ui_fill_round_rect(fb,visible,36,FOLIO_SURFACE);
    folio_ui_text(fb,rect.x+rect.width/2,rect.y+31,30,label,EPD_DRAW_ALIGN_CENTER,enabled?FOLIO_INK:0xAA,FOLIO_SURFACE);
}
static bool weight_supported(unsigned weight) {
    int number=weight==FORMAT_BOLD?700:weight==FORMAT_MEDIUM?500:400;
    return number>=s_format.preview.weight_min&&number<=s_format.preview.weight_max;
}
static void render_size(uint8_t *fb,const folio_format_snapshot_t *s) {
    folio_ui_header(fb,&s_format.status,"字号与字重",true);
    folio_format_ui_preview(fb,(EpdRect){32,200,620,204},&s->candidate,&s_format.preview,"给阅读留一点空间。",NULL,false,false);
    char text[48];snprintf(text,sizeof(text),"当前字号 %u",s->candidate.px);
    folio_ui_text(fb,56,322,26,text,EPD_DRAW_ALIGN_LEFT,(uint8_t)(folio_format_ink(&s->candidate)*17),s->candidate.background==FORMAT_PAPER_WHITE?FOLIO_SURFACE:(uint8_t)(folio_format_paper(&s->candidate)*17));
    folio_ui_text(fb,40,454,30,"字号",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
    size_step(fb,(EpdRect){40,488,140,96},"−",s->candidate.px>36);
    size_step(fb,(EpdRect){504,488,140,96},"+",s->candidate.px<72);
    snprintf(text,sizeof(text),"%u",s->candidate.px);
    folio_ui_text(fb,342,517,40,text,EPD_DRAW_ALIGN_CENTER,FOLIO_INK,0xFF);
    folio_ui_text(fb,40,606,26,"36-72，逐次调整 4 像素",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    folio_ui_text(fb,40,690,30,"字重",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
    for(unsigned i=0;i<3;i++){
        folio_format_ui_radio(fb,(EpdRect){32,742+(int)i*112,620,112},folio_format_ui_weight(i),NULL,s->candidate.weight==i);
        if(!weight_supported(i))folio_ui_text(fb,630,762+(int)i*112,24,"此字体不支持",EPD_DRAW_ALIGN_RIGHT,0xAA,0xFF);
    }
    folio_format_ui_footer(fb,"保留到草稿");
}
static void render_spacing(uint8_t *fb,const folio_format_snapshot_t *s) {
    folio_ui_header(fb,&s_format.status,"行距与页边距",true);
    folio_format_ui_preview(fb,(EpdRect){32,194,620,202},&s->candidate,&s_format.preview,"山风轻轻翻过这一页。","让每一行有舒适的距离。",false,true);
    static const char *spacing[]={"紧凑","标准","宽松"},*margin[]={"窄","标准","宽"},*indent[]={"不缩进","两字"};
    folio_ui_text(fb,40,436,30,"行距",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
    folio_ui_text(fb,40,612,30,"页边距",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
    folio_ui_text(fb,40,788,30,"首行缩进",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
    for(int i=0;i<3;i++){
        folio_format_ui_choice(fb,folio_format_ui_choice_rect(i,3,480),spacing[i],s->candidate.spacing==i);
        folio_format_ui_choice(fb,folio_format_ui_choice_rect(i,3,656),margin[i],s->candidate.margin==i);
    }
    for(int i=0;i<2;i++)folio_format_ui_choice(fb,folio_format_ui_choice_rect(i,2,832),indent[i],s->candidate.indent_em==(unsigned)(i*2));
    folio_format_ui_footer(fb,"保留到草稿");
}
static void render_background(uint8_t *fb,const folio_format_snapshot_t *s) {
    folio_ui_header(fb,&s_format.status,"阅读背景",true);
    folio_ui_text(fb,40,194,26,"只改变正文与页脚，菜单保持白底。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    for(unsigned i=0;i<3;i++){
        int x=40+(int)i*208;folio_format_values_t v=s->candidate;v.background=(uint8_t)i;
        uint8_t paper=(uint8_t)(folio_format_paper(&v)*17),ink=(uint8_t)(folio_format_ink(&v)*17);
        ui_fill_round_rect(fb,(EpdRect){x,274,188,178},18,paper);
        ui_draw_round_rect(fb,(EpdRect){x,274,188,178},18,s->candidate.background==i?FOLIO_INK:FOLIO_RULE);
        if(s->candidate.background==i)ui_draw_round_rect(fb,(EpdRect){x+3,277,182,172},16,FOLIO_INK);
        folio_ui_text(fb,x+94,298,50,"阅",EPD_DRAW_ALIGN_CENTER,ink,paper);
        folio_ui_text(fb,x+94,380,28,folio_format_ui_background(i),EPD_DRAW_ALIGN_CENTER,ink,paper);
    }
    const char *note=s->candidate.background==FORMAT_PAPER_WHITE?"默认白底，文字对比清楚。":s->candidate.background==FORMAT_PAPER_GRAY?"浅灰底仅作用于正文与页脚。":"黑底可能增加残影，应用前请确认。";
    folio_ui_text(fb,40,526,30,note,EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
    folio_ui_text(fb,40,648,28,"统一应用成功后才切换正文背景。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    folio_format_ui_footer(fb,"保留到草稿");
}
static void render(app_ctx_t *ctx,uint8_t *fb) {
    (void)ctx;const folio_format_snapshot_t *s=folio_format_snapshot();
    if(s_format.unavailable||!s){
        folio_ui_header(fb,&s_format.status,"排版会话暂不可用",true);
        folio_ui_text_fit(fb,40,352,30,604,s_format.error,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,448,28,"原来的排版保持不变。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_button(fb,(EpdRect){40,1076,604,96},"返回",false);return;
    }
    if(s_format.overlay==FORMAT_DISCARD_CONFIRM){
        folio_ui_header(fb,&s_format.status,"放弃排版草稿？",true);
        folio_ui_text(fb,40,354,32,"本次所有未提交的字体、字号、行距和",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,404,32,"背景修改都会放弃。",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,500,30,"当前正文与阅读进度状态不受影响。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_button(fb,(EpdRect){40,958,604,96},"继续编辑",false);
        folio_ui_button(fb,(EpdRect){40,1076,604,96},"放弃草稿并返回",true);return;
    }
    if(s_format.overlay==FORMAT_DARK_CONFIRM){
        folio_ui_header(fb,&s_format.status,"保留黑底到草稿？",true);
        folio_ui_text(fb,40,354,32,"黑底可能增加残影。",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,450,30,"这里只保留背景候选值，之前的字体和",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,498,30,"字号草稿仍在。最后统一应用才改变正文。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_button(fb,(EpdRect){40,958,604,96},"返回背景选择",false);
        folio_ui_button(fb,(EpdRect){40,1076,604,96},"保留黑底到草稿",true);return;
    }
    if(s_format.overlay==FORMAT_COMMIT_ERROR||s_format.overlay==FORMAT_SAVING){
        bool busy=s_format.overlay==FORMAT_SAVING;
        folio_ui_header(fb,&s_format.status,busy?(s->scope==FORMAT_DEFAULTS?"正在保存默认":"正在应用排版"):(s_format.discard_failed?"草稿未能关闭":s->scope==FORMAT_DEFAULTS?"默认排版未保存":"排版未能应用"),!busy);
        folio_ui_text(fb,40,354,30,busy?"正在核对资源并保存完整排版。":"原来的排版仍生效，全部草稿已经保留。",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        if(!busy){
            folio_ui_text_fit(fb,40,450,26,604,s_format.error,FOLIO_SECONDARY,0xFF);
            folio_ui_button(fb,(EpdRect){40,958,604,96},"回到草稿",false);
            folio_ui_button(fb,(EpdRect){40,1076,604,96},s_format.discard_failed?"重试放弃":"重试提交",true);
        }
        return;
    }
    switch(s->field){
        case FORMAT_FIELD_SIZE_WEIGHT:render_size(fb,s);break;
        case FORMAT_FIELD_SPACING:render_spacing(fb,s);break;
        case FORMAT_FIELD_BACKGROUND:render_background(fb,s);break;
        default:render_root(fb,s);break;
    }
    if(s->recovered_checkpoint)folio_ui_text_fit(fb,40,156,24,604,"已恢复最后确认的草稿，尚未应用。",FOLIO_SECONDARY,0xFF);
    if(s_format.error[0])folio_ui_text_fit(fb,40,1044,24,604,s_format.error,FOLIO_SECONDARY,0xFF);
}
static app_redraw_t apply(app_ctx_t *ctx) {
    s_format.discard_failed=false;s_format.overlay=FORMAT_SAVING;app_present(ctx,&app_folio_format,APP_REDRAW_PAGE);
    if(folio_format_apply(ctx)){s_format.overlay=FORMAT_NORMAL;return APP_REDRAW_NONE;}
    remember_error();s_format.overlay=FORMAT_COMMIT_ERROR;return APP_REDRAW_PAGE;
}
static app_redraw_t return_editor(app_ctx_t *ctx) {
    const folio_format_snapshot_t *s=folio_format_snapshot();
    if(s&&s->field==FORMAT_FIELD_FONT){ctx->request_app=&app_folio_font_picker;return APP_REDRAW_NONE;}
    prepare_preview();return APP_REDRAW_PAGE;
}
static app_redraw_t cancel(app_ctx_t *ctx) {
    const folio_format_snapshot_t *s=folio_format_snapshot();
    if(s_format.unavailable||!s){ctx->request_return=true;return APP_REDRAW_NONE;}
    if(s_format.overlay==FORMAT_DISCARD_CONFIRM){folio_format_cancel_leave();s_format.menu_after_discard=false;s_format.overlay=FORMAT_NORMAL;return return_editor(ctx);}
    if(s_format.overlay!=FORMAT_NORMAL){if(s_format.discard_failed){folio_format_cancel_leave();s_format.discard_failed=false;s_format.menu_after_discard=false;}s_format.overlay=FORMAT_NORMAL;return return_editor(ctx);}
    if(s->field!=FORMAT_FIELD_NONE){
        if(!folio_format_field_cancel()){remember_error();return APP_REDRAW_PAGE;}
        prepare_preview();return APP_REDRAW_PAGE;
    }
    folio_format_request_leave(s->editor_origin);s_format.overlay=FORMAT_DISCARD_CONFIRM;return APP_REDRAW_PAGE;
}
static app_redraw_t on_gesture(app_ctx_t *ctx,const ui_gesture_event_t *e) {
    if(e->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    int x=e->x0,y=e->y0;const folio_format_snapshot_t *s=folio_format_snapshot();
    if(ui_rect_hit((EpdRect){24,78,96,96},x,y))return cancel(ctx);
    if(s_format.unavailable||!s){if(ui_rect_hit((EpdRect){40,1076,604,96},x,y))return cancel(ctx);return APP_REDRAW_NONE;}
    if(s_format.overlay==FORMAT_DISCARD_CONFIRM){
        if(ui_rect_hit((EpdRect){40,958,604,96},x,y))return cancel(ctx);
        if(ui_rect_hit((EpdRect){40,1076,604,96},x,y)){
            if(folio_format_discard(ctx)){
                if(s_format.menu_after_discard){ctx->request_menu_after_switch=true;s_format.menu_after_discard=false;}
                s_format.overlay=FORMAT_NORMAL;return APP_REDRAW_NONE;
            }
            remember_error();s_format.discard_failed=true;s_format.overlay=FORMAT_COMMIT_ERROR;return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if(s_format.overlay==FORMAT_COMMIT_ERROR){
        if(ui_rect_hit((EpdRect){40,958,604,96},x,y))return cancel(ctx);
        if(ui_rect_hit((EpdRect){40,1076,604,96},x,y)){
            if(s_format.discard_failed){
                if(folio_format_discard(ctx)){if(s_format.menu_after_discard){ctx->request_menu_after_switch=true;s_format.menu_after_discard=false;}s_format.overlay=FORMAT_NORMAL;return APP_REDRAW_NONE;}
                remember_error();return APP_REDRAW_PAGE;
            }
            return apply(ctx);
        }
        return APP_REDRAW_NONE;
    }
    if(s_format.overlay==FORMAT_DARK_CONFIRM){
        if(ui_rect_hit((EpdRect){40,958,604,96},x,y)){s_format.overlay=FORMAT_NORMAL;return APP_REDRAW_PAGE;}
        if(ui_rect_hit((EpdRect){40,1076,604,96},x,y)){
            if(!folio_format_field_keep())remember_error();
            s_format.overlay=FORMAT_NORMAL;prepare_preview();return APP_REDRAW_PAGE;
        }
        return APP_REDRAW_NONE;
    }
    if(s_format.overlay==FORMAT_SAVING)return APP_REDRAW_NONE;
    if(ui_rect_hit((EpdRect){40,1100,290,96},x,y))return cancel(ctx);
    if(ui_rect_hit((EpdRect){346,1100,298,96},x,y)){
        if(s->field==FORMAT_FIELD_NONE)return apply(ctx);
        if(s->field==FORMAT_FIELD_BACKGROUND&&s->candidate.background==FORMAT_PAPER_BLACK){s_format.overlay=FORMAT_DARK_CONFIRM;return APP_REDRAW_PAGE;}
        if(!folio_format_field_keep())remember_error();
        prepare_preview();return APP_REDRAW_PAGE;
    }
    bool changed=false;
    if(s->field==FORMAT_FIELD_NONE){
        static const folio_format_field_t fields[]={FORMAT_FIELD_FONT,FORMAT_FIELD_SIZE_WEIGHT,FORMAT_FIELD_SPACING,FORMAT_FIELD_BACKGROUND};
        for(int i=0;i<4;i++)if(ui_rect_hit((EpdRect){32,422+i*122,620,112},x,y)){
            if(!folio_format_field_begin(fields[i])){remember_error();return APP_REDRAW_PAGE;}
            if(i==0){
                if(folio_format_enter_font_child(false)){ctx->request_app=&app_folio_font_picker;return APP_REDRAW_NONE;}
                remember_error();return APP_REDRAW_PAGE;
            }
            return APP_REDRAW_PAGE;
        }
    }else if(s->field==FORMAT_FIELD_SIZE_WEIGHT){
        if(s->candidate.px>36&&ui_rect_hit((EpdRect){40,488,140,96},x,y))changed=folio_format_size_delta(-4);
        if(s->candidate.px<72&&ui_rect_hit((EpdRect){504,488,140,96},x,y))changed=folio_format_size_delta(4);
        for(int i=0;i<3;i++)if(s->candidate.weight!=(unsigned)i&&weight_supported((unsigned)i)&&ui_rect_hit((EpdRect){32,742+i*112,620,112},x,y))changed=folio_format_set_weight((folio_format_weight_t)i);
    }else if(s->field==FORMAT_FIELD_SPACING){
        for(int i=0;i<3;i++){
            if(s->candidate.spacing!=(unsigned)i&&ui_rect_hit(folio_format_ui_choice_rect(i,3,480),x,y))changed=folio_format_set_spacing((folio_format_spacing_t)i);
            if(s->candidate.margin!=(unsigned)i&&ui_rect_hit(folio_format_ui_choice_rect(i,3,656),x,y))changed=folio_format_set_margin((folio_format_margin_t)i);
        }
        for(int i=0;i<2;i++)if(s->candidate.indent_em!=(unsigned)(i*2)&&ui_rect_hit(folio_format_ui_choice_rect(i,2,832),x,y))changed=folio_format_set_indent((unsigned)i*2);
    }else if(s->field==FORMAT_FIELD_BACKGROUND){
        for(int i=0;i<3;i++)if(s->candidate.background!=(unsigned)i&&ui_rect_hit((EpdRect){40+208*i,274,188,178},x,y))changed=folio_format_set_background((folio_format_background_t)i);
    }
    if(changed){folio_metrics_span_t span=folio_metrics_begin();render(ctx,ctx->fb);folio_metrics_render_end(span);return APP_REDRAW_AREA;}
    return APP_REDRAW_NONE;
}
static bool present(app_ctx_t *ctx,app_redraw_t redraw) {
    if(redraw!=APP_REDRAW_AREA)return false;
    // 同字段改值保留标题/底栏，只提交预览与控件包围区。/ Same-field edits retain header/footer and submit only preview/control bounds.
    EpdRect area={32,194,620,884};
    guard_draw_result(ctx->hl,update_display_area_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,ui_rotate_rect_to_fb(area)));
    return true;
}
void folio_format_request_menu(app_ctx_t *ctx) {
    const folio_format_snapshot_t *s=folio_format_snapshot();
    if(!s||!folio_format_has_session()){ctx->request_menu=true;return;}
    if(!folio_format_request_leave(s->editor_origin)){remember_error();return;}
    s_format.menu_after_discard=true;s_format.overlay=FORMAT_DISCARD_CONFIRM;
    ctx->request_app=&app_folio_format;
}
static app_redraw_t on_key(app_ctx_t *ctx,int key) {
    if(key==UI_KEY_1)return cancel(ctx);
    if(key==UI_KEY_2)return APP_REDRAW_FULL;
    if(key==UI_KEY_3){folio_format_request_menu(ctx);return APP_REDRAW_PAGE;}
    return APP_REDRAW_NONE;
}
static void on_media_lost(app_ctx_t *ctx) {
    folio_format_media_lost(ctx);
    s_format.preview.ready=false;remember_error();
}
static void format_on_exit(app_ctx_t *ctx) {
    (void)ctx;folio_format_set_owner_visible(false);
}
static int format_metrics_scenario(void) {return FOLIO_METRICS_FORMAT;}
const app_desc_t app_folio_format={.metrics_scenario=format_metrics_scenario,.title="字体与排版",.detail="预览草稿后统一应用",.owns_keys=true,.owns_footer=true,.manages_font=true,
    .on_enter=on_enter,.on_exit=format_on_exit,.on_media_lost=on_media_lost,.render=render,.present=present,.on_gesture=on_gesture,.on_key=on_key};
