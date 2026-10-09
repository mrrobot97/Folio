/* SPDX-License-Identifier: Apache-2.0
 * 生产排版/字体页面 + 生产字体引擎；服务 ACK/目录枚举仅用于故障注入。
 * Production format/font pages and glyph engine; service ACKs/catalog enumeration are injected boundaries.
 */
#define FOLIO_UI_REAL_FONT
#include "ui_host_test.c"
#include "format_service.h"
#include "ui_menu.h"
#include "transfer_navigation.h"
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>

static folio_format_snapshot_t model;
static bool fail_apply,fail_discard,fail_child;
static int apply_calls,discard_calls,size_calls,transfer_calls,media_lost_calls;
static const app_desc_t book_destination={.title="阅读"};
static ttf_font_item_t catalog[2];
static size_t heap_used;
static char service_error[128];

void host_log(const char*tag,const char*format,...){(void)tag;(void)format;}
void*heap_caps_malloc(size_t n,unsigned caps){(void)caps;size_t*p=malloc(n+sizeof(size_t));if(!p)return NULL;*p=n;heap_used+=n;return p+1;}
void*heap_caps_calloc(size_t n,size_t size,unsigned caps){void*p=heap_caps_malloc(n*size,caps);if(p)memset(p,0,n*size);return p;}
void heap_caps_free(void*p){if(p){size_t*q=(size_t*)p-1;heap_used-=*q;free(q);}}
size_t heap_caps_get_largest_free_block(unsigned caps){(void)caps;return 8u*1024u*1024u-heap_used;}
int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
const char*app_settings_font_path(void){return "builtin";}
int ttf_font_scan(void){io_reads++;return 2;}
int ttf_font_count(void){return 2;}
const ttf_font_item_t*ttf_font_item(int index){return index>=0&&index<2?&catalog[index]:NULL;}
const folio_format_snapshot_t*folio_format_snapshot(void){return &model;}
const char*folio_format_error(void){return service_error;}
static void start_model(folio_format_scope_t scope,const char*font_path){
    memset(&model,0,sizeof(model));model.scope=scope;model.lifecycle=FORMAT_CLOSED;model.session_id=77;model.active_revision=3;
    model.editor_origin=scope==FORMAT_DEFAULTS?FORMAT_ROUTE_SETTINGS:FORMAT_ROUTE_READER;model.route=FORMAT_ROUTE_ROOT;
    model.active=folio_format_builtin_defaults();
    if(font_path)snprintf(model.active.font.path,sizeof(model.active.font.path),"%s",font_path);
    assert(folio_format_model_begin(&model));
    fail_apply=fail_discard=fail_child=false;service_error[0]=0;
}
bool folio_format_begin_book(app_ctx_t*ctx){start_model(FORMAT_BOOK,catalog[0].path);ctx->request_app=&app_folio_format;return true;}
bool folio_format_begin_defaults(app_ctx_t*ctx){start_model(FORMAT_DEFAULTS,catalog[0].path);ctx->request_app=&app_folio_format;return true;}
bool folio_format_field_begin(folio_format_field_t f){return folio_format_model_field_begin(&model,f);}
bool folio_format_size_delta(int d){size_calls++;int px=model.candidate.px+d;if((d!=4&&d!=-4)||px<36||px>72)return false;folio_format_values_t v=model.candidate;v.px=(uint8_t)px;return folio_format_model_candidate(&model,&v);}
bool folio_format_set_weight(folio_format_weight_t w){folio_format_values_t v=model.candidate;v.weight=(uint8_t)w;return folio_format_model_candidate(&model,&v);}
bool folio_format_set_spacing(folio_format_spacing_t value){folio_format_values_t v=model.candidate;v.spacing=(uint8_t)value;return folio_format_model_candidate(&model,&v);}
bool folio_format_set_margin(folio_format_margin_t value){folio_format_values_t v=model.candidate;v.margin=(uint8_t)value;return folio_format_model_candidate(&model,&v);}
bool folio_format_set_indent(unsigned em){folio_format_values_t v=model.candidate;v.indent_em=(uint8_t)em;return folio_format_model_candidate(&model,&v);}
bool folio_format_set_background(folio_format_background_t bg){folio_format_values_t v=model.candidate;v.background=(uint8_t)bg;return folio_format_model_candidate(&model,&v);}
bool folio_format_set_font(const folio_format_font_t*f){folio_format_values_t v=model.candidate;v.font=*f;return folio_format_model_candidate(&model,&v);}
bool folio_format_field_keep(void){return folio_format_model_field_keep(&model);}
bool folio_format_field_cancel(void){return folio_format_model_field_cancel(&model);}
bool folio_format_apply(app_ctx_t*ctx){apply_calls++;if(fail_apply){snprintf(service_error,sizeof(service_error),"排版保存失败，仍使用原排版；请重试");return false;}assert(model.field==FORMAT_FIELD_NONE);model.active=model.root;model.active_revision++;model.lifecycle=FORMAT_APPLIED;ctx->request_app=model.scope==FORMAT_DEFAULTS?&app_folio_settings:&book_destination;return true;}
bool folio_format_discard(app_ctx_t*ctx){discard_calls++;if(fail_discard){snprintf(service_error,sizeof(service_error),"取消尚未保存，原草稿保留，请重试");return false;}model.lifecycle=FORMAT_DISCARDED;ctx->request_app=model.scope==FORMAT_DEFAULTS?&app_folio_settings:&book_destination;return true;}
bool folio_format_set_view(uint8_t selection,uint8_t cursor,uint8_t scroll){model.selection=selection;model.cursor=cursor;model.scroll=scroll;return true;}
bool folio_format_enter_font_child(bool transfer){if(fail_child){snprintf(service_error,sizeof(service_error),"字体子流程暂存失败，草稿仍保留");return false;}model.font_child=true;if(transfer){model.return_stack[model.return_depth++]=FORMAT_ROUTE_FONT;model.route=FORMAT_ROUTE_FONT_TRANSFER;model.font_transfer_stopped=false;}return true;}
bool folio_format_leave_font_child(bool stopped){if(!stopped)return false;if(model.route==FORMAT_ROUTE_FONT_TRANSFER){model.route=FORMAT_ROUTE_FONT;model.font_transfer_stopped=true;if(model.return_depth)model.return_depth--;}else model.font_child=false;return true;}
bool folio_format_request_leave(folio_format_route_t dest){model.requested_leave=dest;return true;}
void folio_format_cancel_leave(void){model.requested_leave=0;}
bool folio_format_return_to_editor(app_ctx_t*ctx){ctx->request_app=model.field==FORMAT_FIELD_FONT?&app_folio_font_picker:&app_folio_format;return true;}
void folio_format_media_lost(app_ctx_t*ctx){(void)ctx;media_lost_calls++;model.resource_missing=true;snprintf(service_error,sizeof(service_error),"图书或字体资源已断开，完整排版草稿已保留");}
void folio_format_set_owner_visible(bool visible){(void)visible;}
bool folio_format_has_session(void){return model.lifecycle==FORMAT_EDITING;}
void app_transfer_open_font_child(app_ctx_t*ctx,const app_desc_t*return_page){assert(return_page==&app_folio_font_picker);transfer_calls++;ctx->request_app=&app_transfer;}
bool app_transfer_is_font_child(void){return model.route==FORMAT_ROUTE_FONT_TRANSFER;}
void app_present(app_ctx_t*ctx,const app_desc_t*app,app_redraw_t redraw){if(redraw!=APP_REDRAW_NONE)app->render(ctx,ctx->fb);}
static void capture(const app_desc_t*app,app_ctx_t*ctx,const char*out,const char*name){
    folio_format_snapshot_t old=model;ttf_font_cache_stats_t before_roles[TTF_ROLE_COUNT];
    for(int i=0;i<TTF_ROLE_COUNT;i++)ttf_font_cache_stats((ttf_font_role_t)i,&before_roles[i]);
    draw(app,ctx,out,name);assert(!memcmp(&old,&model,sizeof(model)));
    for(int i=0;i<TTF_ROLE_COUNT;i++){ttf_font_cache_stats_t after;ttf_font_cache_stats((ttf_font_role_t)i,&after);assert(after.opens==before_roles[i].opens);}
}
int main(int argc,char**argv){
    assert(argc==3||argc==4);const char*out=argv[2];char broken[160];snprintf(broken,sizeof(broken),"%s/broken.ttf",out);FILE*f=fopen(broken,"wb");assert(f);fputs("invalid font fixture",f);fclose(f);
    snprintf(catalog[0].path,sizeof(catalog[0].path),"%s",argv[1]);snprintf(catalog[0].name,sizeof(catalog[0].name),"ChillDuanSansVF.ttf");
    snprintf(catalog[1].path,sizeof(catalog[1].path),"%s",broken);snprintf(catalog[1].name,sizeof(catalog[1].name),"损坏测试字体.ttf");
    assert(ttf_font_init()==ESP_OK);ttf_font_scope_t ui_scope;assert(ttf_font_begin(TTF_ROLE_UI,&ui_scope)==ESP_OK);assert(ttf_font_check_text("像素背景紧凑宽松未应用草稿当前字号无法预览，请调整字号。当前字体不支持此字重，请调整字重。此字体不支持草稿字重，请返回后调整。已恢复最后确认的草稿，尚未应用。",NULL));ttf_font_end(&ui_scope);assert(ttf_font_prepare(TTF_ROLE_READER,argv[1])==ESP_OK);
    app_ctx_t ctx={.fb=fb};summary.today_day=20261009;
    start_model(FORMAT_BOOK,argv[1]);app_folio_format.on_enter(&ctx);capture(&app_folio_format,&ctx,out,"R04-book-root");
    tap(&app_folio_format,&ctx,200,590);capture(&app_folio_format,&ctx,out,"R14-size");assert(tap(&app_folio_format,&ctx,200,790)==APP_REDRAW_NONE);memcpy(before,fb,sizeof(fb));assert(tap(&app_folio_format,&ctx,575,535)==APP_REDRAW_AREA);update_count=0;assert(app_folio_format.present(&ctx,APP_REDRAW_AREA)&&update_count==1);size_t changed=0;for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(before[y*W+x]!=fb[y*W+x]){changed++;assert(ui_rect_hit(updates[0],x,y));}assert(updates[0].width*updates[0].height==548080);printf("Format field: one GL16 region, 548080/831744=65.90%% logical pixels; %zu changed pixels inside.\n",changed);assert(model.candidate.px==48&&model.root.px==44&&model.active.px==44);
    tap(&app_folio_format,&ctx,200,890);assert(model.candidate.weight==FORMAT_MEDIUM&&model.candidate.px==48);capture(&app_folio_format,&ctx,out,"R14-size-weight-candidate");
    tap(&app_folio_format,&ctx,170,1140);assert(model.root.px==44&&model.active.px==44);tap(&app_folio_format,&ctx,200,590);tap(&app_folio_format,&ctx,575,535);tap(&app_folio_format,&ctx,495,1140);assert(model.root.px==48&&model.active.px==44);
    capture(&app_folio_format,&ctx,out,"R04-edited");tap(&app_folio_format,&ctx,200,710);capture(&app_folio_format,&ctx,out,"R15-standard");tap(&app_folio_format,&ctx,545,530);tap(&app_folio_format,&ctx,135,706);tap(&app_folio_format,&ctx,188,880);assert(model.candidate.spacing==FORMAT_LOOSE&&model.candidate.margin==FORMAT_MARGIN_NARROW&&model.candidate.indent_em==0);capture(&app_folio_format,&ctx,out,"R15-spacing");tap(&app_folio_format,&ctx,495,1140);assert(model.root.px==48&&model.root.spacing==FORMAT_LOOSE&&model.active.spacing==FORMAT_STANDARD);
    tap(&app_folio_format,&ctx,200,836);tap(&app_folio_format,&ctx,340,330);capture(&app_folio_format,&ctx,out,"R11-gray");tap(&app_folio_format,&ctx,550,330);tap(&app_folio_format,&ctx,495,1140);capture(&app_folio_format,&ctx,out,"R11-dark-confirm");assert(model.root.background==FORMAT_PAPER_WHITE);tap(&app_folio_format,&ctx,300,1120);assert(model.root.background==FORMAT_PAPER_BLACK&&model.active.background==FORMAT_PAPER_WHITE);
    tap(&app_folio_format,&ctx,170,1140);capture(&app_folio_format,&ctx,out,"R24-discard-confirm");tap(&app_folio_format,&ctx,300,1000);assert(model.root.px==48&&model.root.background==FORMAT_PAPER_BLACK);
    fail_apply=true;int before_apply=apply_calls;tap(&app_folio_format,&ctx,495,1140);assert(apply_calls==before_apply+1&&model.active.px==44&&model.lifecycle==FORMAT_EDITING);capture(&app_folio_format,&ctx,out,"FMT-ERROR-transaction");tap(&app_folio_format,&ctx,300,1000);assert(model.root.px==48);
    fail_apply=false;tap(&app_folio_format,&ctx,495,1140);assert(model.active.px==48&&ctx.request_app==&book_destination);
    start_model(FORMAT_DEFAULTS,argv[1]);app_folio_format.on_enter(&ctx);capture(&app_folio_format,&ctx,out,"R04-defaults");tap(&app_folio_format,&ctx,200,590);model.candidate.px=36;capture(&app_folio_format,&ctx,out,"R14-size-min-disabled");int calls=size_calls;assert(tap(&app_folio_format,&ctx,110,535)==APP_REDRAW_NONE&&size_calls==calls);tap(&app_folio_format,&ctx,575,535);assert(model.candidate.px==40);model.candidate.px=72;capture(&app_folio_format,&ctx,out,"R14-size-max-disabled");calls=size_calls;assert(tap(&app_folio_format,&ctx,575,535)==APP_REDRAW_NONE&&size_calls==calls);tap(&app_folio_format,&ctx,110,535);assert(model.candidate.px==68);
    app_folio_format.on_key(&ctx,UI_KEY_3);capture(&app_folio_format,&ctx,out,"R24-defaults-interrupted-field");tap(&app_folio_format,&ctx,300,1000);assert(model.field==FORMAT_FIELD_SIZE_WEIGHT&&model.candidate.px==68);app_folio_format.on_key(&ctx,UI_KEY_3);fail_discard=true;before_apply=apply_calls;tap(&app_folio_format,&ctx,300,1120);capture(&app_folio_format,&ctx,out,"FMT-discard-failure");fail_discard=false;tap(&app_folio_format,&ctx,300,1120);assert(apply_calls==before_apply&&model.lifecycle==FORMAT_DISCARDED&&ctx.request_app==&app_folio_settings&&ctx.request_menu_after_switch);
    start_model(FORMAT_DEFAULTS,argv[1]);app_folio_format.on_enter(&ctx);tap(&app_folio_format,&ctx,200,590);tap(&app_folio_format,&ctx,575,535);tap(&app_folio_format,&ctx,495,1140);capture(&app_folio_format,&ctx,out,"R04-defaults-edited");tap(&app_folio_format,&ctx,495,1140);assert(model.lifecycle==FORMAT_APPLIED&&model.active.px==48&&ctx.request_app==&app_folio_settings);
    start_model(FORMAT_BOOK,argv[1]);ctx.request_app=NULL;ctx.request_menu_after_switch=false;app_folio_format.on_enter(&ctx);app_folio_format.on_key(&ctx,UI_KEY_3);tap(&app_folio_format,&ctx,300,1120);assert(model.lifecycle==FORMAT_DISCARDED&&ctx.request_app==&book_destination&&ctx.request_menu_after_switch);
    start_model(FORMAT_BOOK,argv[1]);app_folio_format.on_enter(&ctx);tap(&app_folio_format,&ctx,200,470);assert(ctx.request_app==&app_folio_font_picker);app_folio_font_picker.on_enter(&ctx);capture(&app_folio_font_picker,&ctx,out,"R10-font-picker");app_folio_font_picker.on_key(&ctx,UI_KEY_3);assert(ctx.request_app==&app_folio_format);app_folio_format.on_enter(&ctx);tap(&app_folio_format,&ctx,300,1000);assert(ctx.request_app==&app_folio_font_picker&&model.field==FORMAT_FIELD_FONT);app_folio_font_picker.on_enter(&ctx);uint64_t identity=model.session_id;tap(&app_folio_font_picker,&ctx,200,390);capture(&app_folio_font_picker,&ctx,out,"S05-font-error");assert(!strcmp(model.candidate.font.path,argv[1])&&!strcmp(model.active.font.path,argv[1]));tap(&app_folio_font_picker,&ctx,300,1000);
    tap(&app_folio_font_picker,&ctx,200,515);capture(&app_folio_font_picker,&ctx,out,"FONT-COVERAGE-builtin");tap(&app_folio_font_picker,&ctx,300,1120);assert(!strcmp(model.candidate.font.path,"builtin")&&!strcmp(model.root.font.path,argv[1]));
    fail_child=true;int transfers=transfer_calls;tap(&app_folio_font_picker,&ctx,300,960);assert(transfer_calls==transfers);capture(&app_folio_font_picker,&ctx,out,"FONT-child-checkpoint-error");fail_child=false;tap(&app_folio_font_picker,&ctx,300,1000);tap(&app_folio_font_picker,&ctx,300,960);assert(transfer_calls==transfers+1&&model.route==FORMAT_ROUTE_FONT_TRANSFER&&model.session_id==identity);app_folio_font_picker.on_enter(&ctx);assert(transfer_calls==transfers+1&&model.route==FORMAT_ROUTE_FONT&&model.session_id==identity&&!strcmp(model.candidate.font.path,"builtin"));capture(&app_folio_font_picker,&ctx,out,"FONT-return-from-transfer");tap(&app_folio_font_picker,&ctx,495,1140);assert(!strcmp(model.root.font.path,"builtin")&&!strcmp(model.active.font.path,argv[1]));
    start_model(FORMAT_DEFAULTS,"/missing/original-font.ttf");folio_format_field_begin(FORMAT_FIELD_FONT);folio_format_enter_font_child(false);app_folio_font_picker.on_enter(&ctx);capture(&app_folio_font_picker,&ctx,out,"FMT-RESOURCE-MISSING");assert(!strcmp(model.candidate.font.path,"/missing/original-font.ttf"));tap(&app_folio_font_picker,&ctx,300,1120);capture(&app_folio_font_picker,&ctx,out,"FONT-missing-list-no-fallback");
    app_folio_font_picker.on_media_lost(&ctx);assert(media_lost_calls==1&&model.resource_missing);capture(&app_folio_font_picker,&ctx,out,"FONT-media-lost-candidate-retained");
    if(argc==4){start_model(FORMAT_BOOK,argv[3]);app_folio_format.on_enter(&ctx);tap(&app_folio_format,&ctx,200,590);capture(&app_folio_format,&ctx,out,"R14-static-weight-disabled");assert(tap(&app_folio_format,&ctx,200,890)==APP_REDRAW_NONE&&model.candidate.weight==FORMAT_REGULAR);}
    start_model(FORMAT_BOOK,argv[1]);folio_format_field_begin(FORMAT_FIELD_SIZE_WEIGHT);folio_format_size_delta(4);model.recovered_checkpoint=true;app_folio_format.on_enter(&ctx);capture(&app_folio_format,&ctx,out,"FMT-recovered-field-candidate");assert(model.candidate.px==48&&model.active.px==44);
    app_folio_settings.on_enter(&ctx);tap(&app_folio_settings,&ctx,200,445);capture(&app_folio_settings,&ctx,out,"S02-default-entry");
    for(int i=0;i<TTF_ROLE_COUNT;i++){ttf_font_scope_t scope;if(ttf_font_begin((ttf_font_role_t)i,&scope)==ESP_OK){ttf_font_unload();ttf_font_end(&scope);}}
    assert(heap_used==0);puts("PASS: production format callbacks/font renderer, no render opens; candidate/root/active, failed ACK, exact cancel, scopes, endpoints, font child lineage and missing-resource routes.");return 0;
}
