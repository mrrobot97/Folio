/* SPDX-License-Identifier: Apache-2.0
 * 排版控件复用生产 UI 原语，不拥有草稿或文件。/ Format controls reuse production primitives and own no drafts or files.
 */
#include "format_ui.h"
#include "ui_kit.h"

void folio_format_ui_footer(uint8_t *fb, const char *keep_label) {
    folio_ui_button(fb,(EpdRect){40,1100,290,96},"取消",false);
    folio_ui_button(fb,(EpdRect){346,1100,298,96},keep_label,true);
}
EpdRect folio_format_ui_choice_rect(int index,int count,int y) {
    if(count<1||count>3||index<0||index>=count)return (EpdRect){0};
    int width=(604-(count-1)*12)/count;
    return (EpdRect){40+index*(width+12),y,width,96};
}
void folio_format_ui_choice(uint8_t *fb,EpdRect rect,const char *label,bool selected) {
    folio_ui_button(fb,rect,label,selected);
}
void folio_format_ui_radio(uint8_t *fb,EpdRect rect,const char *label,const char *detail,bool selected) {
    int center=rect.y+40;
    epd_draw_circle(60,center,13,FOLIO_INK,fb);
    epd_draw_circle(60,center,12,FOLIO_INK,fb);
    if(selected)epd_fill_circle(60,center,7,FOLIO_INK,fb);
    folio_ui_text_fit(fb,96,rect.y+16,34,538,label,FOLIO_INK,0xFF);
    if(detail)folio_ui_text_fit(fb,96,rect.y+60,26,538,detail,FOLIO_SECONDARY,0xFF);
    ui_hairline(fb,rect.y+rect.height-1,40,604,FOLIO_RULE);
}
bool folio_format_ui_cancel_hit(int x,int y) {
    return ui_rect_hit((EpdRect){24,78,96,96},x,y)||ui_rect_hit((EpdRect){40,1100,290,96},x,y);
}

#include "ttf_font.h"
#include <stdio.h>
#include <string.h>

void folio_format_ui_font_name(const char *path,char *out,size_t capacity) {
    if(!out||capacity==0)return;
    const char *name=path;
    if(!path||!*path||strcmp(path,TTF_FONT_BUILTIN)==0)name="内置界面字体";
    else {const char *slash=strrchr(path,'/');if(slash)name=slash+1;}
    size_t n=strlen(name);
    if(n>=capacity){n=capacity-1;while(n&&((unsigned char)name[n]&0xC0)==0x80)n--;}
    memcpy(out,name,n);out[n]=0;
}
const char *folio_format_ui_background(unsigned bg) {
    static const char *names[]={"白底","浅灰底","黑底"};
    return bg<3?names[bg]:"未知";
}
const char *folio_format_ui_weight(unsigned weight) {
    static const char *names[]={"常规","适中","加粗"};
    return weight<3?names[weight]:"未知";
}
bool folio_format_ui_prepare(const folio_format_values_t *v,folio_format_preview_t *p) {
    memset(p,0,sizeof(*p));
    if(!v)return false;
    folio_format_ui_font_name(v->font.path,p->name,sizeof(p->name));
    p->builtin=!v->font.path[0]||strcmp(v->font.path,TTF_FONT_BUILTIN)==0;
    ttf_font_scope_t scope;
    if(ttf_font_prepare(TTF_ROLE_PREVIEW,p->builtin?TTF_FONT_BUILTIN:v->font.path)!=ESP_OK)return false;
    if(ttf_font_begin(TTF_ROLE_PREVIEW,&scope)!=ESP_OK)return false;
    p->ready=true;
    p->variable_weight=ttf_font_weight_range(&p->weight_min,&p->weight_max);
    p->sample_complete=ttf_font_check_text("山风轻轻翻过这一页。慢一点，也能抵达远方。Reading, at your pace.",&p->first_missing);
    ttf_font_end(&scope);
    return true;
}
static void preview_text(uint8_t *fb,int x,int y,int em,int px,int width,const char *text,uint8_t ink,uint8_t paper) {
    char line[192];size_t n=0;const unsigned char *p=(const unsigned char*)text;
    while(*p){
        size_t bytes=*p<0x80?1:(*p&0xE0)==0xC0?2:(*p&0xF0)==0xE0?3:4;
        if(n+bytes+4>=sizeof(line)||strlen((const char*)p)<bytes)break;
        memcpy(line+n,p,bytes);line[n+bytes]=0;
        if(ttf_text_width_px(px,line)>width)break;
        n+=bytes;p+=bytes;
    }
    line[n]=0;
    ttf_draw_text_px(fb,x,y+(em*88+50)/100,px,line,EPD_DRAW_ALIGN_LEFT,ink>>4,paper>>4);
}
void folio_format_ui_preview(uint8_t *fb,EpdRect box,const folio_format_values_t *v,
    const folio_format_preview_t *p,const char *first,const char *second,bool caption,bool paragraph_layout) {
    uint8_t paper=v?(uint8_t)(folio_format_paper(v)*17):0xFF,ink=v?(uint8_t)(folio_format_ink(v)*17):FOLIO_INK;
    if(v&&!caption&&v->background==FORMAT_PAPER_WHITE)paper=FOLIO_SURFACE;
    ui_fill_round_rect(fb,box,20,paper);
    ui_draw_round_rect(fb,box,20,FOLIO_RULE);
    if(!v||!p->ready){
        folio_ui_text_fit(fb,box.x+24,box.y+40,28,box.width-48,"候选字体暂不可用，原排版仍保留。",ink,paper);
        return;
    }
    ttf_font_scope_t scope;
    if(ttf_font_begin(TTF_ROLE_PREVIEW,&scope)!=ESP_OK){
        folio_ui_text_fit(fb,box.x+24,box.y+40,28,box.width-48,"字体资源已失效，请重新检查。",ink,paper);
        return;
    }
    ttf_set_weight(folio_format_weight_number(v));
    int px=ttf_font_pixel_height_for_em(v->px),margin=paragraph_layout?folio_format_margin_pixels(v):24;
    if(px<=0){ttf_font_end(&scope);folio_ui_text_fit(fb,box.x+24,box.y+40,28,box.width-48,"当前字号无法预览，请调整字号。",ink,paper);return;}
    int inset=paragraph_layout?v->indent_em*v->px:0;
    int y=box.y+24;
    preview_text(fb,box.x+margin+inset,y,v->px,px,box.width-2*margin-inset,first,ink,paper);
    int line=folio_format_line_height(v,v->px);
    if(second){
        int second_em=((unsigned char)second[0]<0x80&&v->px>30)?30:v->px;
        int second_px=ttf_font_pixel_height_for_em(second_em);
        if(second_px>0&&y+line+second_em<box.y+box.height-(caption?40:12))
            preview_text(fb,box.x+margin,y+line,second_em,second_px,box.width-2*margin,second,ink,paper);
    }
    ttf_font_end(&scope);
    if(caption){
        char label[160];snprintf(label,sizeof(label),"%s · %u · %s",p->name,v->px,folio_format_ui_background(v->background));
        folio_ui_text_fit(fb,box.x+24,box.y+box.height-40,24,box.width-48,label,ink,paper);
    }
}
