/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 完整排版编辑协调：纯候选、资源验证、设备预备和持久 ACK。
 * Full format coordination: pure candidates, resource validation, device preparation and durable ACK.
 * 冻结：任何失败都不激活候选；恢复不启动传输；DEFAULTS 不改正在打开的书。
 * Frozen: failure never activates candidates, restore never starts transfer, DEFAULTS never changes an open book.
 */
#include "format_service.h"
#include "format_store.h"
#include "reader_runtime.h"
#include "reader_draft.h"
#include "book_progress.h"
#include "settings.h"
#include "ttf_font.h"
#include "transfer_navigation.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifndef FORMAT_DIRECTORY
#define FORMAT_DIRECTORY "/flash/folio"
#endif
extern const app_desc_t app_folio_format,app_folio_font_picker;
static folio_format_snapshot_t s_format;
static char s_error[160];
static bool s_owner_visible;
static bool s_power_owner;
static uint64_t s_power_session;
static bool s_read_baseline_pending;
static folio_format_snapshot_t s_read_baseline;
void folio_format_reset_runtime(void){memset(&s_format,0,sizeof(s_format));memset(&s_read_baseline,0,sizeof(s_read_baseline));s_read_baseline_pending=false;s_error[0]=0;s_owner_visible=s_power_owner=false;s_power_session=0;folio_format_store_reset_runtime();}
void folio_format_capture_power_origin(const app_desc_t* origin){
    extern const app_desc_t app_transfer;
    s_power_session=s_format.session_id;
    s_power_owner=folio_format_has_session()&&(origin==&app_folio_format||origin==&app_folio_font_picker||
        (origin==&app_transfer&&app_transfer_is_font_child()&&s_format.font_child));
}
static bool fail(const char* message){snprintf(s_error,sizeof(s_error),"%s",message);return false;}
static void clear_error(void){s_error[0]=0;s_format.resource_missing=false;}
const folio_format_snapshot_t* folio_format_snapshot(void){return &s_format;}
const char* folio_format_error(void){return s_error;}
bool folio_format_has_session(void){return s_format.lifecycle==FORMAT_EDITING;}
bool folio_format_read_baseline_pending(const char* path){return s_read_baseline_pending&&(!path||!strcmp(path,s_read_baseline.book_path));}
bool folio_format_flush_read_baseline(void){
    if(!s_read_baseline_pending)return true;
    folio_format_snapshot_t confirmed=s_read_baseline;confirmed.transient_baseline=false;
    if(!folio_format_store_seed_baseline(FORMAT_DIRECTORY,&confirmed))return fail(folio_format_store_status()==FORMAT_STORE_CAPACITY?"新书排版尚未保存：128 本记录上限，旧数据未删除":"新书排版尚未保存，请检查空间或存储；RAM 保留");
    s_read_baseline_pending=false;return true;
}
void folio_format_set_owner_visible(bool visible){s_owner_visible=visible;}
void folio_format_media_lost(app_ctx_t* ctx){
    (void)ctx;folio_reader_format_media_lost();s_format.resource_missing=true;
    fail("图书或字体资源已断开，完整排版草稿已保留");
}
static bool identity_read(const char* path,folio_format_font_t* out){
    ttf_font_identity_t id;if(ttf_font_identity_read(path,&id)!=ESP_OK)return false;
    memset(out,0,sizeof(*out));snprintf(out->path,sizeof(out->path),"%s",id.path);
    out->file_size=id.file_size;out->mtime=id.mtime;out->header_crc=id.header_crc;return true;
}
static bool font_valid(const folio_format_font_t* expected){
    folio_format_font_t actual;if(!identity_read(expected->path,&actual))return fail("字体不可用，请接回原资源或重新选择");
    if(actual.file_size!=expected->file_size||actual.mtime!=expected->mtime||actual.header_crc!=expected->header_crc)
        return fail("字体文件已更改，请重新选择并确认");
    return true;
}
static bool resources_valid(const folio_format_snapshot_t* s,const folio_format_values_t* value){
    if(s->scope==FORMAT_BOOK){struct stat st;if(stat(s->book_path,&st)||!S_ISREG(st.st_mode)||(uint64_t)st.st_size!=s->book_size||st.st_mtime!=s->book_mtime)return fail("原图书缺失或已更改，草稿已保留");}
    return font_valid(&value->font);
}
static folio_format_values_t legacy_defaults(void){
    folio_format_values_t value=folio_format_builtin_defaults();
    const char* path=app_settings_font_path();if(!path||!path[0])path=TTF_FONT_BUILTIN;
    if(!identity_read(path,&value.font)){memset(&value.font,0,sizeof(value.font));snprintf(value.font.path,sizeof(value.font.path),"%s",path);}
    return value;
}
static int legacy_em_candidate(const char* path,int native) {
    if(ttf_font_prepare(TTF_ROLE_PREVIEW,path)!=ESP_OK)return 0;
    ttf_font_scope_t font;if(ttf_font_begin(TTF_ROLE_PREVIEW,&font)!=ESP_OK)return 0;
    int best=36,distance=100000;
    for(int em=36;em<=72;em+=4){int px=ttf_font_pixel_height_for_em(em);if(!px)continue;int delta=px>native?px-native:native-px;if(delta<distance){best=em;distance=delta;}}
    ttf_font_end(&font);ttf_font_discard_preview();return distance==100000?0:best;
}
static void baseline(folio_format_snapshot_t* s,folio_format_scope_t scope,const char* path,uint64_t size,int64_t mtime,const folio_format_values_t* value){
    memset(s,0,sizeof(*s));s->scope=scope;s->lifecycle=FORMAT_CLOSED;s->route=scope==FORMAT_BOOK?FORMAT_ROUTE_READER:FORMAT_ROUTE_SETTINGS;
    snprintf(s->book_path,sizeof(s->book_path),"%s",path?path:"");s->book_size=size;s->book_mtime=mtime;
    s->active=s->root=s->field_entry=s->candidate=*value;s->active_revision=1;s->font_transfer_stopped=true;
}
bool folio_format_resolve_book(const char* path,uint64_t size,int64_t mtime,bool previously_read,uint8_t progress_format,
        const folio_format_values_t* legacy,folio_format_values_t* out,uint64_t* revision){
    if(!path||!legacy||!out||!revision)return false;
    if(s_read_baseline_pending&&!strcmp(path,s_read_baseline.book_path)){
        if(s_read_baseline.book_size!=size||s_read_baseline.book_mtime!=mtime)return fail("待保存新书已更改，RAM 排版基线保留");
        *out=s_read_baseline.active;*revision=0;return true;
    }
    folio_format_snapshot_t stored;folio_checkpoint_result_t result=folio_format_store_load(FORMAT_DIRECTORY,FORMAT_BOOK,path,&stored);
    if(result==FOLIO_CHECKPOINT_OK){
        if(stored.book_size!=size||stored.book_mtime!=mtime)return fail("本书排版记录与文件不匹配，原记录已保留");
        if(!font_valid(&stored.active.font))return false;
        *out=stored.active;*revision=stored.active_revision;return true;
    }
    if(result!=FOLIO_CHECKPOINT_MISSING)return fail("本书排版记录无法读取，未覆盖原记录");
    if(previously_read&&progress_format>BOOK_PROGRESS_FOLIO_NATIVE)return fail("进度的排版来源未知，未猜测字号或字体");
    if(previously_read&&(progress_format==BOOK_PROGRESS_FOLIO_EM||progress_format==BOOK_PROGRESS_FOLIO_NATIVE))return fail("本书新版排版记录缺失，进度已保留；未回退旧字号或字体");
    bool transient_resume=previously_read&&progress_format==BOOK_PROGRESS_TRANSIENT_DEFAULTS;
    if(transient_resume)previously_read=false;
    // 新名字空间一旦完整确认就始终优先，旧 M1 记录只在确实不存在时读取。
    // A confirmed new-namespace record always wins; consult M1 only when the new record is truly missing.
    folio_reader_draft_t old;folio_checkpoint_result_t migrated=transient_resume?FOLIO_CHECKPOINT_MISSING:folio_reader_draft_load_result(FORMAT_DIRECTORY,path,&old);
    if(migrated!=FOLIO_CHECKPOINT_OK&&migrated!=FOLIO_CHECKPOINT_MISSING)return fail("旧排版记录损坏或版本未知，未覆盖原文件");
    bool has_old=migrated==FOLIO_CHECKPOINT_OK;
    if(has_old&&(old.file_size!=size||(old.book_mtime&&old.book_mtime!=(uint64_t)mtime)))return fail("旧草稿与图书身份冲突，原记录保留");
    folio_format_values_t value=*legacy;bool uses_legacy=true;
    if(has_old)value.px=old.applied_px;
    else if(!previously_read){value.px=44;value.legacy_native_px=0;}
    if(!previously_read&&!has_old){
        result=folio_format_store_load(FORMAT_DIRECTORY,FORMAT_DEFAULTS,"",&stored);
        if(result==FOLIO_CHECKPOINT_OK){value=stored.active;uses_legacy=false;}
        else if(result!=FOLIO_CHECKPOINT_MISSING)return fail("默认排版记录无法读取，未使用猜测值");
    }
    if(uses_legacy){
        const char* source=has_old&&old.editing&&old.font_path[0]?old.font_path:legacy->font.path;
        if(!identity_read(source,&value.font))return fail("实际生效的旧字体缺失，请恢复原资源");
        if(has_old&&old.editing&&!ttf_font_path_is_builtin(old.font_path)&&old.font_file_size&&value.font.file_size!=old.font_file_size)return fail("旧草稿字体大小已变化，未迁移或替换原记录");
        value.indent_em=0;
        if(has_old||previously_read){int native=value.px;int em=legacy_em_candidate(value.font.path,native);if(!em)return fail("旧字号度量无法恢复，原记录保留");value.px=em;value.legacy_native_px=native;}
    }
    if(!font_valid(&value.font))return false;
    baseline(&stored,FORMAT_BOOK,path,size,mtime,&value);
    const char* title=strrchr(path,'/');snprintf(stored.book_title,sizeof(stored.book_title),"%s",title?title+1:path);
    if(has_old&&old.editing){
        if(!folio_format_model_begin(&stored))return fail("旧草稿迁移准备失败");
        int draft_em=legacy_em_candidate(value.font.path,old.draft_px);if(!draft_em)return fail("旧草稿字号度量无法恢复");
        stored.root.px=draft_em;stored.root.legacy_native_px=old.draft_px;stored.field_entry=stored.candidate=stored.root;
        stored.root_dirty=!folio_format_values_equal(&stored.active,&stored.root);
    }
    // 完整新包 ACK 就是持久迁移标记；旧文件保留但不再拥有来源优先权。
    // The full new-envelope ACK is the durable migration marker; retain old files without restoring their precedence.
    bool saved=stored.lifecycle==FORMAT_CLOSED?folio_format_store_seed_baseline(FORMAT_DIRECTORY,&stored):folio_format_store_save(FORMAT_DIRECTORY,&stored);
    if(!saved){
        bool passive=!previously_read&&!has_old&&!folio_reader_has_unapplied_draft()&&stored.lifecycle==FORMAT_CLOSED;
        if(passive&&!s_read_baseline_pending){s_read_baseline=stored;s_read_baseline.generation=0;s_read_baseline.transient_baseline=true;s_read_baseline_pending=true;*out=value;*revision=0;fail("排版未确认保存；重开以可恢复记录或默认值为准");return true;}
        return fail(folio_format_store_status()==FORMAT_STORE_CAPACITY?"排版记录已达 128 本上限，旧记录未删除":"排版记录未保存，请检查剩余空间；旧文件与草稿保留");
    }
    *out=value;*revision=stored.active_revision;return true;
}
static bool protect_current(void){
    if(!folio_format_has_session())return true;
    bool ok=folio_format_store_save(FORMAT_DIRECTORY,&s_format);
    if(!ok&&(folio_format_store_status()==FORMAT_STORE_UNKNOWN||folio_format_store_status()==FORMAT_STORE_COMMITTED_NEEDS_FINALIZE))s_format.intent_attempted=true;
    return ok||fail("上一份排版草稿未保存，暂不能切换编辑目标");
}
bool folio_format_prepare_book_change(const char* next_path){
    if(!next_path)return false;
    if(s_read_baseline_pending&&strcmp(next_path,s_read_baseline.book_path)){
        if(!folio_format_flush_read_baseline()&&(!folio_format_has_session()||(!s_format.edited&&!s_format.intent_attempted&&!s_format.commit_unknown)))s_read_baseline_pending=false;
    }
    if(!folio_format_has_session()||(s_format.scope==FORMAT_BOOK&&!strcmp(next_path,s_format.book_path)))return true;
    return protect_current();
}
bool folio_format_begin_book(app_ctx_t* ctx){
    if(!ctx)return false;
    folio_format_snapshot_t captured;if(!folio_reader_format_capture(&captured))return fail("请先打开原图书，再修改本书排版");
    if(folio_format_has_session()&&s_format.scope==FORMAT_BOOK&&!strcmp(s_format.book_path,captured.book_path)){
        folio_reader_format_retain(true);ctx->request_app=&app_folio_format;return true;
    }
    if(!protect_current())return false;
    folio_format_snapshot_t existing;folio_checkpoint_result_t result=folio_format_store_load(FORMAT_DIRECTORY,FORMAT_BOOK,captured.book_path,&existing);
    if(result==FOLIO_CHECKPOINT_OK){if(existing.book_size!=captured.book_size||existing.book_mtime!=captured.book_mtime)return fail("图书身份冲突，保留原草稿");captured=existing;}
    else if(result!=FOLIO_CHECKPOINT_MISSING)return fail("无法读取本书排版，原记录已保留");
    if(!folio_format_model_begin(&captured))return fail("排版会话无法开始");
    s_format=captured;clear_error();folio_reader_format_retain(true);ctx->request_app=&app_folio_format;return true;
}
bool folio_format_recover_reader_font(app_ctx_t* ctx){
    if(!folio_format_begin_book(ctx))return false;
    if(s_format.field==FORMAT_FIELD_NONE&&!folio_format_field_begin(FORMAT_FIELD_FONT))return false;
    if(s_format.field!=FORMAT_FIELD_FONT)return fail("当前字体无法显示正文，请先处理原排版子页");
    if(!folio_format_enter_font_child(false))return false;
    s_format.resource_missing=true;fail("当前字体缺少正文字形或资源不可读，请选择完整字体");
    ctx->request_app=&app_folio_font_picker;return true;
}
bool folio_format_open_book_recovery(app_ctx_t* ctx,const char* path){
    if(!ctx||!path)return false;
    if(folio_format_has_session()&&(s_format.scope!=FORMAT_BOOK||strcmp(s_format.book_path,path))&&!protect_current())return false;
    folio_format_snapshot_t stored;
    if(folio_format_store_load(FORMAT_DIRECTORY,FORMAT_BOOK,path,&stored)!=FOLIO_CHECKPOINT_OK)return false;
    struct stat book;if(stat(path,&book)||!S_ISREG(book.st_mode)||(uint64_t)book.st_size!=stored.book_size||book.st_mtime!=stored.book_mtime)return fail("图书身份已变化，原排版记录保留");
    if(!folio_format_has_session()||strcmp(s_format.book_path,path)){if(!folio_format_model_begin(&stored))return false;s_format=stored;}
    if(s_format.field==FORMAT_FIELD_NONE&&!folio_format_field_begin(FORMAT_FIELD_FONT))return false;
    if(s_format.field!=FORMAT_FIELD_FONT)return folio_format_return_to_editor(ctx);
    if(!folio_format_enter_font_child(false))return false;
    s_format.resource_missing=true;fail("原正文字体不可用，请明确选择完整字体后应用");
    folio_reader_format_retain(true);ctx->request_app=&app_folio_font_picker;return true;
}
bool folio_format_restore_book_session(app_ctx_t* ctx,const char* path){
    if(!ctx||!path)return false;
    if(folio_format_has_session())return folio_format_return_to_editor(ctx);
    folio_format_snapshot_t stored;
    if(folio_format_store_load(FORMAT_DIRECTORY,FORMAT_BOOK,path,&stored)!=FOLIO_CHECKPOINT_OK||stored.lifecycle!=FORMAT_EDITING)return fail("这本书没有未关闭的排版草稿");
    s_format=stored;s_format.recovered_checkpoint=true;clear_error();
    if(!resources_valid(&s_format,s_format.field==FORMAT_FIELD_NONE?&s_format.root:&s_format.candidate))s_format.resource_missing=true;
    return folio_format_return_to_editor(ctx);
}
bool folio_format_begin_defaults(app_ctx_t* ctx){
    if(!ctx)return false;
    if(folio_format_has_session()&&s_format.scope==FORMAT_DEFAULTS){ctx->request_app=&app_folio_format;return true;}
    if(!protect_current())return false;
    folio_format_snapshot_t candidate;folio_checkpoint_result_t result=folio_format_store_load(FORMAT_DIRECTORY,FORMAT_DEFAULTS,"",&candidate);
    if(result==FOLIO_CHECKPOINT_MISSING){folio_format_values_t value=legacy_defaults();baseline(&candidate,FORMAT_DEFAULTS,"",0,0,&value);}
    else if(result!=FOLIO_CHECKPOINT_OK)return fail("默认排版记录无法读取，原文件已保留");
    if(!folio_format_model_begin(&candidate))return fail("默认排版会话无法开始");
    s_format=candidate;clear_error();ctx->request_app=&app_folio_format;return true;
}
bool folio_format_field_begin(folio_format_field_t field){if(!folio_format_model_field_begin(&s_format,field))return fail("当前排版子页无法打开");clear_error();return true;}
static bool set_candidate(const folio_format_values_t* value){if(!folio_format_model_candidate(&s_format,value))return fail("排版值无效或不属于当前字段");clear_error();return true;}
bool folio_format_size_delta(int delta){if(s_format.field!=FORMAT_FIELD_SIZE_WEIGHT||(delta!=-4&&delta!=4))return false;int px=s_format.candidate.px+delta;if(px<36||px>72)return false;folio_format_values_t v=s_format.candidate;v.px=px;v.legacy_native_px=0;return set_candidate(&v);}
bool folio_format_set_weight(folio_format_weight_t weight){folio_format_values_t v=s_format.candidate;v.weight=weight;return set_candidate(&v);}
bool folio_format_set_spacing(folio_format_spacing_t spacing){folio_format_values_t v=s_format.candidate;v.spacing=spacing;return set_candidate(&v);}
bool folio_format_set_margin(folio_format_margin_t margin){folio_format_values_t v=s_format.candidate;v.margin=margin;return set_candidate(&v);}
bool folio_format_set_indent(unsigned em){if(em!=0&&em!=2)return false;folio_format_values_t v=s_format.candidate;v.indent_em=em;return set_candidate(&v);}
bool folio_format_set_background(folio_format_background_t background){folio_format_values_t v=s_format.candidate;v.background=background;return set_candidate(&v);}
bool folio_format_set_font(const folio_format_font_t* font){if(!font||!font_valid(font))return false;folio_format_values_t v=s_format.candidate;v.font=*font;return set_candidate(&v);}
bool folio_format_field_keep(void){if(!folio_format_model_field_keep(&s_format))return fail("请先完成字体子流程");clear_error();return true;}
bool folio_format_field_cancel(void){if(!folio_format_model_field_cancel(&s_format))return fail("请先返回排版子页");clear_error();return true;}
bool folio_format_set_view(uint8_t selection,uint8_t cursor,uint8_t scroll){if(!folio_format_has_session()||s_format.revision==UINT64_MAX)return false;s_format.selection=selection;s_format.cursor=cursor;s_format.scroll=scroll;++s_format.revision;return true;}
bool folio_format_enter_font_child(bool transfer){
    if(!folio_format_has_session()||s_format.field!=FORMAT_FIELD_FONT)return false;
    folio_format_snapshot_t next=s_format;
    if(transfer){if(next.return_depth==FOLIO_FORMAT_STACK_MAX)return false;next.return_stack[next.return_depth++]=FORMAT_ROUTE_FONT;next.route=FORMAT_ROUTE_FONT_TRANSFER;next.font_transfer_stopped=false;}
    else{next.route=FORMAT_ROUTE_FONT;next.font_transfer_stopped=true;}
    next.font_child=true;next.font_resource=next.candidate.font;
    // 缺失资源可进入上传修复；先持久关联身份/返回栈，不把资源存在作为偷偷换字体的许可。
    // Missing resources may enter upload repair; first persist identity/return lineage, never authorize font substitution.
    if(transfer&&!folio_format_store_save(FORMAT_DIRECTORY,&next))return fail("字体上传来源未能保存，尚未开始传输");
    s_format=next;
    if(transfer&&!folio_format_store_finalize(FORMAT_DIRECTORY)){
        (void)folio_format_transfer_stopped();return fail("字体上传恢复确认未完成，尚未开始传输");
    }
    return true;
}
bool folio_format_leave_font_child(bool stopped){
    if(!folio_format_has_session()||s_format.field!=FORMAT_FIELD_FONT)return false;
    if(s_format.route==FORMAT_ROUTE_FONT_TRANSFER){if(!stopped)return fail("字体传输尚未安全停止");s_format.font_transfer_stopped=true;s_format.route=FORMAT_ROUTE_FONT;if(s_format.return_depth)--s_format.return_depth;}
    else{s_format.font_child=false;s_format.font_transfer_stopped=stopped;}
    return true;
}
bool folio_format_transfer_stopped(void){
    if(!folio_format_has_session()||!s_format.font_child||s_format.field!=FORMAT_FIELD_FONT)return false;
    if(s_format.route==FORMAT_ROUTE_FONT_TRANSFER){s_format.route=FORMAT_ROUTE_FONT;s_format.font_transfer_stopped=true;if(s_format.return_depth&&s_format.return_stack[s_format.return_depth-1]==FORMAT_ROUTE_FONT)--s_format.return_depth;return true;}
    if(s_format.route==FORMAT_ROUTE_FONT){s_format.font_transfer_stopped=true;return true;}
    return false;
}
bool folio_format_request_leave(folio_format_route_t destination){if(!folio_format_has_session())return false;s_format.requested_leave=destination;return true;}
void folio_format_cancel_leave(void){s_format.requested_leave=0;}
bool folio_format_apply(app_ctx_t* ctx){
    if(!ctx||!folio_format_has_session()||s_format.field!=FORMAT_FIELD_NONE||s_format.font_child)return fail("请先保留或取消当前子页草稿");
    if(!resources_valid(&s_format,&s_format.root)){s_format.resource_missing=true;return false;}
    if(!folio_reader_format_prepare(ctx,&s_format.root,s_format.scope))return fail(folio_reader_format_prepare_error());
    folio_format_snapshot_t committed=s_format;if(committed.active_revision==UINT64_MAX){folio_reader_format_abort();return fail("排版版本已达上限");}
    committed.active=committed.root;committed.field_entry=committed.candidate=committed.active;++committed.active_revision;
    committed.lifecycle=FORMAT_APPLIED;committed.root_dirty=committed.field_dirty=false;committed.requested_leave=0;
    committed.transient_baseline=false;
    if(!folio_format_store_transition(FORMAT_DIRECTORY,&s_format,&committed)){folio_reader_format_abort();folio_format_store_status_t status=folio_format_store_status();if(status==FORMAT_STORE_UNKNOWN||status==FORMAT_STORE_COMMITTED_NEEDS_FINALIZE)s_format.intent_attempted=true;return fail(status==FORMAT_STORE_CAPACITY?"排版记录已达 128 本上限，旧记录未删除":status==FORMAT_STORE_UNKNOWN||status==FORMAT_STORE_COMMITTED_NEEDS_FINALIZE?"应用结果未确认，当前仍用原排版；草稿保留":"保存失败，请检查剩余空间；旧记录与草稿保留");}
    // 所有可能失败的字体/分页准备在持久 ACK 前完成，提交只发布已准备资源。
    // Complete fallible font/layout preparation before durable ACK; commit only publishes prepared resources.
    if(!folio_reader_format_commit(&committed.active,committed.active_revision,committed.scope))return fail("排版已保存，显示资源需重新载入");
    s_format=committed;if(committed.scope==FORMAT_BOOK&&s_read_baseline_pending&&!strcmp(committed.book_path,s_read_baseline.book_path))s_read_baseline_pending=false;clear_error();folio_reader_format_return(ctx,committed.scope);return true;
}
bool folio_format_discard(app_ctx_t* ctx){
    if(!ctx||!folio_format_has_session()||(s_format.font_child&&!s_format.font_transfer_stopped))return fail("请先停止字体传输");
    folio_format_snapshot_t closed=s_format;closed.lifecycle=FORMAT_DISCARDED;closed.root=closed.field_entry=closed.candidate=closed.active;
    closed.field=FORMAT_FIELD_NONE;closed.root_dirty=closed.field_dirty=closed.font_child=false;
    // 明确确认 Discard 可放弃从未写入意图的纯 RAM 编辑，不等同自动丢失草稿。
    // Explicit confirmed Discard may abandon RAM edits with no attempted durable intent; this is never automatic draft loss.
    if(s_format.transient_baseline&&!s_format.intent_attempted&&!s_format.commit_unknown){s_format=closed;clear_error();folio_reader_format_abort();folio_reader_format_return(ctx,closed.scope);return true;}
    closed.transient_baseline=false;
    if(!folio_format_store_transition(FORMAT_DIRECTORY,&s_format,&closed)){if(folio_format_store_status()==FORMAT_STORE_UNKNOWN||folio_format_store_status()==FORMAT_STORE_COMMITTED_NEEDS_FINALIZE)s_format.intent_attempted=true;return fail("取消结果未确认，原草稿保留，请重试");}
    s_format=closed;if(closed.scope==FORMAT_BOOK&&s_read_baseline_pending&&!strcmp(closed.book_path,s_read_baseline.book_path))s_read_baseline_pending=false;clear_error();folio_reader_format_abort();folio_reader_format_return(ctx,closed.scope);return true;
}
bool folio_format_return_to_editor(app_ctx_t* ctx){if(!ctx||!folio_format_has_session())return false;ctx->request_app=s_format.field==FORMAT_FIELD_FONT?&app_folio_font_picker:&app_folio_format;return true;}
bool folio_format_prepare_sleep(bool eco){
    bool baseline_ok=folio_format_flush_read_baseline();
    bool passive=s_read_baseline_pending&&(!folio_format_has_session()||(s_format.transient_baseline&&!s_format.edited&&!s_format.intent_attempted&&!s_format.commit_unknown));
    if(!baseline_ok&&passive){
        folio_format_snapshot_t old_origin;
        if(eco&&folio_format_store_resume(FORMAT_DIRECTORY,&old_origin)==FOLIO_CHECKPOINT_OK)return fail("已有待恢复的排版来源，请先处理后休眠");
        fail("本书排版未确认保存；重开以可恢复记录或默认值为准");return true;
    }
    if(!baseline_ok&&!folio_format_has_session())return false;
    if(!folio_format_has_session())return !eco||folio_format_store_clear_resume(FORMAT_DIRECTORY);
    if(s_format.font_child&&!s_format.font_transfer_stopped)return fail("字体传输尚未安全停止，不能休眠");
    if(eco&&(!s_power_owner||s_power_session!=s_format.session_id))return fail("未应用排版仍在，请先返回编辑后休眠");
    bool ok=eco?folio_format_store_suspend(FORMAT_DIRECTORY,&s_format):folio_format_store_save(FORMAT_DIRECTORY,&s_format);
    if(!ok&&(folio_format_store_status()==FORMAT_STORE_UNKNOWN||folio_format_store_status()==FORMAT_STORE_COMMITTED_NEEDS_FINALIZE))s_format.intent_attempted=true;
    if(ok&&s_format.scope==FORMAT_BOOK&&s_read_baseline_pending&&!strcmp(s_format.book_path,s_read_baseline.book_path))s_read_baseline_pending=false;
    return ok||fail("完整排版会话与来源未能保存，请重试或返回编辑");
}
void folio_format_wake(void){if(folio_format_has_session()&&s_format.route==FORMAT_ROUTE_FONT_TRANSFER){s_format.route=FORMAT_ROUTE_FONT;s_format.font_transfer_stopped=true;}}
const app_desc_t* folio_format_restore_boot(void){
    folio_format_snapshot_t restored;folio_checkpoint_result_t result=folio_format_store_recover_pending(FORMAT_DIRECTORY,&restored);
    if(result==FOLIO_CHECKPOINT_MISSING)result=folio_format_store_resume(FORMAT_DIRECTORY,&restored);
    if(result!=FOLIO_CHECKPOINT_OK)return NULL;
    s_format=restored;clear_error();s_format.recovered_checkpoint=true;
    if(restored.commit_unknown)fail("上次应用确认未完成；原设置与待确认草稿均已保留");
    const folio_format_values_t* value=s_format.field==FORMAT_FIELD_NONE?&s_format.root:&s_format.candidate;
    if(!resources_valid(&s_format,value))s_format.resource_missing=true;
    if(s_format.field==FORMAT_FIELD_FONT){s_format.route=FORMAT_ROUTE_FONT;s_format.font_transfer_stopped=true;return &app_folio_font_picker;}
    return &app_folio_format;
}
