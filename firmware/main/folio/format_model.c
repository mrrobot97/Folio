/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 排版值校验与尺寸/灰度转换，不读取文件或改变字体。
 * Format validation and metric/grayscale mapping without file or font mutation.
 * 冻结：44 em 的标准行距为64，标准边距40；字重不改变字号。
 * Frozen: at 44 em use 64-pixel standard leading and 40-pixel standard margins; weight never changes size.
 */
#include "format_model.h"
#include <string.h>

folio_format_values_t folio_format_builtin_defaults(void) {
    folio_format_values_t v = {0};
    memcpy(v.font.path, "builtin", 8);
    v.px=44;v.weight=FORMAT_REGULAR;v.spacing=FORMAT_STANDARD;v.margin=FORMAT_MARGIN_STANDARD;
    v.indent_em=2;v.background=FORMAT_PAPER_WHITE;v.header_visible=v.footer_visible=true;
    return v;
}
bool folio_format_values_valid(const folio_format_values_t* v) {
    return v&&memchr(v->font.path,0,sizeof(v->font.path))&&v->font.path[0]&&
        v->px>=36&&v->px<=72&&(v->px-36)%4==0&&v->weight<=FORMAT_BOLD&&
        v->spacing<=FORMAT_LOOSE&&v->margin<=FORMAT_MARGIN_WIDE&&
        (!v->legacy_native_px||(v->legacy_native_px>=36&&v->legacy_native_px<=72&&(v->legacy_native_px-36)%4==0))&&
        (v->indent_em==0||v->indent_em==2)&&v->background<=FORMAT_PAPER_BLACK;
}
bool folio_format_values_equal(const folio_format_values_t* a,const folio_format_values_t* b) {
    return a&&b&&!strcmp(a->font.path,b->font.path)&&a->font.file_size==b->font.file_size&&
        a->font.mtime==b->font.mtime&&a->font.header_crc==b->font.header_crc&&
        a->px==b->px&&a->legacy_native_px==b->legacy_native_px&&a->weight==b->weight&&a->spacing==b->spacing&&a->margin==b->margin&&
        a->indent_em==b->indent_em&&a->background==b->background&&
        a->header_visible==b->header_visible&&a->footer_visible==b->footer_visible;
}
int folio_format_weight_number(const folio_format_values_t* v) { return v->weight==FORMAT_BOLD?700:v->weight==FORMAT_MEDIUM?500:400; }
int folio_format_line_height(const folio_format_values_t* v,int px) {
    if(v->legacy_native_px){px=v->legacy_native_px+(px-v->px);if(v->spacing==FORMAT_STANDARD)return px+px/2;}
    int numerator=v->spacing==FORMAT_COMPACT?14:v->spacing==FORMAT_LOOSE?19:16;
    return (px*numerator+5)/11;
}
int folio_format_margin_pixels(const folio_format_values_t* v) { return v->margin==FORMAT_MARGIN_NARROW?24:v->margin==FORMAT_MARGIN_WIDE?64:40; }
uint8_t folio_format_paper(const folio_format_values_t* v) { return v->background==FORMAT_PAPER_BLACK?1:v->background==FORMAT_PAPER_GRAY?13:15; }
uint8_t folio_format_ink(const folio_format_values_t* v) { return v->background==FORMAT_PAPER_BLACK?15:1; }
bool folio_format_model_begin(folio_format_snapshot_t* s) {
    if(!s||!folio_format_values_valid(&s->active)||s->session_id==UINT64_MAX||
       (s->scope!=FORMAT_BOOK&&s->scope!=FORMAT_DEFAULTS))return false;
    if(s->lifecycle==FORMAT_EDITING)return true;
    ++s->session_id;s->revision=1;s->lifecycle=FORMAT_EDITING;
    s->root=s->field_entry=s->candidate=s->active;s->field=FORMAT_FIELD_NONE;
    s->editor_origin=s->scope==FORMAT_BOOK?FORMAT_ROUTE_READER:FORMAT_ROUTE_SETTINGS;
    s->route=FORMAT_ROUTE_ROOT;s->return_depth=1;s->return_stack[0]=s->editor_origin;
    s->requested_leave=0;s->font_child=s->root_dirty=s->field_dirty=false;s->font_transfer_stopped=true;
    s->edited=s->intent_attempted=false;
    return true;
}
bool folio_format_model_field_begin(folio_format_snapshot_t* s,folio_format_field_t field) {
    if(!s||s->lifecycle!=FORMAT_EDITING||s->field!=FORMAT_FIELD_NONE||field<FORMAT_FIELD_FONT||field>FORMAT_FIELD_BACKGROUND||
       s->return_depth==FOLIO_FORMAT_STACK_MAX||s->revision==UINT64_MAX)return false;
    s->field=field;s->field_entry=s->candidate=s->root;s->field_dirty=false;s->field_parent=s->route;
    s->return_stack[s->return_depth++]=s->route;
    s->route=field==FORMAT_FIELD_FONT?FORMAT_ROUTE_FONT:field==FORMAT_FIELD_SIZE_WEIGHT?FORMAT_ROUTE_SIZE:
        field==FORMAT_FIELD_SPACING?FORMAT_ROUTE_SPACING:FORMAT_ROUTE_BACKGROUND;
    s->selection=s->cursor=s->scroll=0;++s->revision;return true;
}
bool folio_format_model_candidate(folio_format_snapshot_t* s,const folio_format_values_t* value) {
    if(!s||!value||s->lifecycle!=FORMAT_EDITING||s->field==FORMAT_FIELD_NONE||s->revision==UINT64_MAX||!folio_format_values_valid(value))return false;
    folio_format_values_t allowed=s->field_entry;
    switch(s->field){
        case FORMAT_FIELD_FONT:allowed.font=value->font;break;
        case FORMAT_FIELD_SIZE_WEIGHT:allowed.px=value->px;allowed.legacy_native_px=value->legacy_native_px;allowed.weight=value->weight;break;
        case FORMAT_FIELD_SPACING:allowed.spacing=value->spacing;allowed.margin=value->margin;allowed.indent_em=value->indent_em;break;
        case FORMAT_FIELD_BACKGROUND:allowed.background=value->background;break;
        default:return false;
    }
    if(!folio_format_values_equal(&allowed,value))return false;
    if(folio_format_values_equal(&s->candidate,value))return true;
    s->candidate=*value;s->field_dirty=!folio_format_values_equal(&s->candidate,&s->field_entry);s->edited=true;++s->revision;return true;
}
static bool close_field(folio_format_snapshot_t* s,bool keep) {
    if(!s||s->lifecycle!=FORMAT_EDITING||s->field==FORMAT_FIELD_NONE||s->font_child||s->revision==UINT64_MAX)return false;
    if(keep)s->root=s->candidate;
    s->candidate=s->field_entry=s->root;s->field=FORMAT_FIELD_NONE;s->field_dirty=false;
    s->root_dirty=!folio_format_values_equal(&s->active,&s->root);
    s->route=s->field_parent;if(s->return_depth)--s->return_depth;++s->revision;return true;
}
bool folio_format_model_field_keep(folio_format_snapshot_t* s){return close_field(s,true);}
bool folio_format_model_field_cancel(folio_format_snapshot_t* s){return close_field(s,false);}
