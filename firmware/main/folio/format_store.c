/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 完整排版会话使用新名字空间，保留 M1 layout_* 原记录用于明确迁移。
 * Full format sessions use a new namespace, retaining M1 layout_* records for explicit migration.
 * 冻结：未知版本不覆盖；活动修订和关闭草稿共享一次 ACK；来源必须匹配完整会话代数。
 * Frozen: never overwrite unknown versions; active revision and draft closure share one ACK; origin must match session generation.
 */
#include "format_store.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
static void* workspace(size_t bytes){
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return calloc(1,bytes);
#endif
}
#define FORMAT_WIRE_SIZE 2048
#define RESUME_WIRE_SIZE 320
#define FORMAT_BOOK_LIMIT 128
static void put(unsigned char** p,uint64_t value,unsigned n){for(unsigned i=0;i<n;i++)*(*p)++=(unsigned char)(value>>(8*i));}
static uint64_t get(const unsigned char** p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)*(*p)++<<(8*i);return v;}
static void put_font(unsigned char** p,const folio_format_font_t* f){memcpy(*p,f->path,sizeof(f->path));*p+=sizeof(f->path);put(p,f->file_size,8);put(p,(uint64_t)f->mtime,8);put(p,f->header_crc,4);}
static void get_font(const unsigned char** p,folio_format_font_t* f){memcpy(f->path,*p,sizeof(f->path));*p+=sizeof(f->path);f->file_size=get(p,8);f->mtime=(int64_t)get(p,8);f->header_crc=(uint32_t)get(p,4);}
static void put_values(unsigned char** p,const folio_format_values_t* v){put_font(p,&v->font);put(p,v->px,1);put(p,v->legacy_native_px,1);put(p,v->weight,1);put(p,v->spacing,1);put(p,v->margin,1);put(p,v->indent_em,1);put(p,v->background,1);put(p,v->header_visible,1);put(p,v->footer_visible,1);}
static bool get_values(const unsigned char** p,folio_format_values_t* v){get_font(p,&v->font);v->px=get(p,1);v->legacy_native_px=get(p,1);v->weight=get(p,1);v->spacing=get(p,1);v->margin=get(p,1);v->indent_em=get(p,1);v->background=get(p,1);unsigned h=get(p,1),f=get(p,1);v->header_visible=h;v->footer_visible=f;return h<2&&f<2&&folio_format_values_valid(v);}
static bool stem(folio_format_scope_t scope,const char* path,char out[32]){
    if(scope==FORMAT_DEFAULTS){snprintf(out,32,"format_defaults");return !path||!path[0];}
    if(scope!=FORMAT_BOOK||!path||!path[0]||strlen(path)>=FOLIO_FORMAT_BOOK_PATH_MAX)return false;
    uint64_t hash=UINT64_C(14695981039346656037);for(const unsigned char* p=(const unsigned char*)path;*p;p++)hash=(hash^*p)*UINT64_C(1099511628211);
    snprintf(out,32,"format_%016llx",(unsigned long long)hash);return true;
}
static bool valid(const folio_format_snapshot_t* s){
    if(!s||!memchr(s->book_path,0,sizeof(s->book_path))||!memchr(s->book_title,0,sizeof(s->book_title))||
       !memchr(s->font_resource.path,0,sizeof(s->font_resource.path))||
       (s->scope!=FORMAT_BOOK&&s->scope!=FORMAT_DEFAULTS)||s->lifecycle>FORMAT_DISCARDED||
       s->field>FORMAT_FIELD_BACKGROUND||s->return_depth>FOLIO_FORMAT_STACK_MAX||
       s->route<FORMAT_ROUTE_READER||s->route>FORMAT_ROUTE_FONT_TRANSFER||
       (s->scope==FORMAT_BOOK&&!s->book_path[0])||(s->scope==FORMAT_DEFAULTS&&s->book_path[0]))return false;
    if(!folio_format_values_valid(&s->active)||!folio_format_values_valid(&s->root)||
       !folio_format_values_valid(&s->field_entry)||!folio_format_values_valid(&s->candidate))return false;
    if(s->lifecycle==FORMAT_EDITING&&(!s->session_id||!s->revision))return false;
    for(unsigned i=0;i<s->return_depth;i++)if(s->return_stack[i]<FORMAT_ROUTE_READER||s->return_stack[i]>FORMAT_ROUTE_FONT_TRANSFER)return false;
    if(s->font_child&&s->field!=FORMAT_FIELD_FONT)return false;
    return true;
}
void folio_format_store_encode(const folio_format_snapshot_t* s,unsigned char data[FORMAT_WIRE_SIZE]){
    memset(data,0,FORMAT_WIRE_SIZE);unsigned char* p=data;
    put(&p,s->scope,1);put(&p,s->lifecycle,1);put(&p,s->field,1);put(&p,s->route,1);
    put(&p,s->session_id,8);put(&p,s->revision,8);put(&p,s->active_revision,8);put(&p,s->suspension_id,8);put(&p,s->transaction_id,8);
    memcpy(p,s->book_path,sizeof(s->book_path));p+=sizeof(s->book_path);memcpy(p,s->book_title,sizeof(s->book_title));p+=sizeof(s->book_title);
    put(&p,s->book_size,8);put(&p,(uint64_t)s->book_mtime,8);
    put_values(&p,&s->active);put_values(&p,&s->root);put_values(&p,&s->field_entry);put_values(&p,&s->candidate);
    put(&p,s->editor_origin,1);put(&p,s->field_parent,1);put(&p,s->requested_leave,1);
    put(&p,s->selection,1);put(&p,s->cursor,1);put(&p,s->scroll,1);put(&p,s->return_depth,1);
    memcpy(p,s->return_stack,sizeof(s->return_stack));p+=sizeof(s->return_stack);
    put(&p,s->font_child,1);put(&p,s->font_transfer_stopped,1);put(&p,s->transient_baseline,1);put(&p,s->edited,1);put(&p,s->intent_attempted,1);put_font(&p,&s->font_resource);
}
bool folio_format_store_decode(const unsigned char data[FORMAT_WIRE_SIZE],folio_format_snapshot_t* s){
    memset(s,0,sizeof(*s));const unsigned char* p=data;
    s->scope=get(&p,1);s->lifecycle=get(&p,1);s->field=get(&p,1);s->route=get(&p,1);
    s->session_id=get(&p,8);s->revision=get(&p,8);s->active_revision=get(&p,8);s->suspension_id=get(&p,8);s->transaction_id=get(&p,8);
    memcpy(s->book_path,p,sizeof(s->book_path));p+=sizeof(s->book_path);memcpy(s->book_title,p,sizeof(s->book_title));p+=sizeof(s->book_title);
    s->book_size=get(&p,8);s->book_mtime=(int64_t)get(&p,8);
    if(!get_values(&p,&s->active)||!get_values(&p,&s->root)||!get_values(&p,&s->field_entry)||!get_values(&p,&s->candidate))return false;
    s->editor_origin=get(&p,1);s->field_parent=get(&p,1);s->requested_leave=get(&p,1);s->selection=get(&p,1);s->cursor=get(&p,1);s->scroll=get(&p,1);s->return_depth=get(&p,1);
    memcpy(s->return_stack,p,sizeof(s->return_stack));p+=sizeof(s->return_stack);
    unsigned child=get(&p,1),stopped=get(&p,1),transient=get(&p,1),edited=get(&p,1),intent=get(&p,1);s->font_child=child;s->font_transfer_stopped=stopped;s->transient_baseline=transient;s->edited=edited;s->intent_attempted=intent;get_font(&p,&s->font_resource);
    while(p<data+FORMAT_WIRE_SIZE)if(*p++)return false;
    s->root_dirty=!folio_format_values_equal(&s->active,&s->root);
    s->field_dirty=s->field!=FORMAT_FIELD_NONE&&!folio_format_values_equal(&s->field_entry,&s->candidate);
    return child<2&&stopped<2&&transient<2&&edited<2&&intent<2&&valid(s);
}
folio_checkpoint_result_t folio_format_store_raw_load(const char* dir,folio_format_scope_t scope,const char* path,folio_format_snapshot_t* out){
    char name[32];if(!out||!stem(scope,path,name))return FOLIO_CHECKPOINT_IO;
    unsigned char* data=workspace(FORMAT_WIRE_SIZE);
    folio_format_snapshot_t* candidate=workspace(sizeof(*candidate));
    if(!data||!candidate){free(data);free(candidate);return FOLIO_CHECKPOINT_IO;}
    size_t len=0;uint64_t generation=0;
    folio_checkpoint_result_t result=folio_checkpoint_load(dir,name,3,data,FORMAT_WIRE_SIZE,&len,&generation);
    if(result==FOLIO_CHECKPOINT_OK){
        if(len!=FORMAT_WIRE_SIZE||!folio_format_store_decode(data,candidate)||candidate->scope!=scope||strcmp(candidate->book_path,path?path:""))result=FOLIO_CHECKPOINT_CORRUPT;
        else{candidate->generation=generation;*out=*candidate;}
    }
    free(data);free(candidate);return result;
}
static bool within_quota(const char* dir){
    DIR* stream=opendir(dir);if(!stream)return false;unsigned count=0;struct dirent* entry;
    while((entry=readdir(stream))){size_t n=strlen(entry->d_name);if(n!=25||strncmp(entry->d_name,"format_",7)||entry->d_name[23]!='.')continue;
        if(entry->d_name[24]=='a')++count;else if(entry->d_name[24]=='b'){char path[384];struct stat st;int len=snprintf(path,sizeof(path),"%s/%s",dir,entry->d_name);if(len<0||(size_t)len>=sizeof(path)){closedir(stream);return false;}path[len-1]='a';if(stat(path,&st))++count;}}
    return closedir(stream)==0&&count<FORMAT_BOOK_LIMIT;
}
bool folio_format_store_raw_save(const char* dir,folio_format_snapshot_t* s){
    char name[32];if(!valid(s)||!stem(s->scope,s->book_path,name))return false;
    folio_format_snapshot_t* previous=workspace(sizeof(*previous));
    unsigned char* data=workspace(FORMAT_WIRE_SIZE);
    if(!previous||!data){free(previous);free(data);return false;}
    folio_checkpoint_result_t loaded=folio_format_store_raw_load(dir,s->scope,s->book_path,previous);
    bool ok=loaded==FOLIO_CHECKPOINT_OK||loaded==FOLIO_CHECKPOINT_MISSING;
    if(loaded==FOLIO_CHECKPOINT_OK&&(previous->book_size!=s->book_size||previous->book_mtime!=s->book_mtime||previous->generation!=s->generation))ok=false;
    if(loaded==FOLIO_CHECKPOINT_MISSING&&(s->generation||(s->scope==FORMAT_BOOK&&!within_quota(dir))))ok=false;
    if(ok){folio_format_store_encode(s,data);uint64_t generation=s->generation;
        ok=folio_checkpoint_save(dir,name,3,data,FORMAT_WIRE_SIZE,&generation)==FOLIO_CHECKPOINT_OK;
        if(ok)s->generation=generation;
    }
    free(previous);free(data);return ok;
}
bool folio_format_store_suspend(const char* dir,folio_format_snapshot_t* s){
    if(!s||s->lifecycle!=FORMAT_EDITING||s->suspension_id==UINT64_MAX||(s->font_child&&!s->font_transfer_stopped))return false;
    folio_format_snapshot_t candidate=*s;++candidate.suspension_id;
    if(candidate.route==FORMAT_ROUTE_FONT_TRANSFER)candidate.route=FORMAT_ROUTE_FONT;
    if(!folio_format_store_save(dir,&candidate))return false;
    // 会话已写入但来源写失败时仍更新代数供重试；不能把它当完整休眠 ACK。
    // Retain the saved generation after origin failure for retry, without claiming a complete suspension ACK.
    *s=candidate;
    if(!folio_format_store_finalize(dir))return false;
    unsigned char ref[RESUME_WIRE_SIZE]={0};unsigned char* p=ref;put(&p,candidate.scope,1);memcpy(p,candidate.book_path,sizeof(candidate.book_path));p+=sizeof(candidate.book_path);
    put(&p,candidate.session_id,8);put(&p,candidate.suspension_id,8);put(&p,candidate.generation,8);uint64_t gen=0;
    if(!folio_format_store_require_space(FOLIO_FORMAT_REFERENCE_RESERVE))return false;
    if(folio_checkpoint_save(dir,"format_resume_v2",1,ref,sizeof(ref),&gen)!=FOLIO_CHECKPOINT_OK)return false;
    *s=candidate;return true;
}
folio_checkpoint_result_t folio_format_store_resume(const char* dir,folio_format_snapshot_t* out){
    unsigned char ref[RESUME_WIRE_SIZE];size_t len=0;uint64_t gen=0;folio_checkpoint_result_t result=folio_checkpoint_load(dir,"format_resume_v2",1,ref,sizeof(ref),&len,&gen);
    if(result!=FOLIO_CHECKPOINT_OK)return result;
    if(len!=sizeof(ref))return FOLIO_CHECKPOINT_CORRUPT;
    const unsigned char* p=ref;folio_format_scope_t scope=get(&p,1);char path[FOLIO_FORMAT_BOOK_PATH_MAX];memcpy(path,p,sizeof(path));p+=sizeof(path);
    if(scope==0)return FOLIO_CHECKPOINT_MISSING;
    if(!memchr(path,0,sizeof(path)))return FOLIO_CHECKPOINT_CORRUPT;
    uint64_t session=get(&p,8),suspension=get(&p,8),generation=get(&p,8);folio_format_snapshot_t candidate;
    result=folio_format_store_load(dir,scope,path,&candidate);if(result!=FOLIO_CHECKPOINT_OK)return result;
    if(candidate.lifecycle!=FORMAT_EDITING||candidate.session_id!=session||candidate.suspension_id!=suspension||candidate.generation!=generation)return FOLIO_CHECKPOINT_CORRUPT;
    candidate.recovered_checkpoint=true;if(candidate.route==FORMAT_ROUTE_FONT_TRANSFER)candidate.route=FORMAT_ROUTE_FONT;*out=candidate;return FOLIO_CHECKPOINT_OK;
}
bool folio_format_store_clear_resume(const char* dir){if(!folio_format_store_require_space(FOLIO_FORMAT_REFERENCE_RESERVE))return false;unsigned char ref[RESUME_WIRE_SIZE]={0};uint64_t gen=0;return folio_checkpoint_save(dir,"format_resume_v2",1,ref,sizeof(ref),&gen)==FOLIO_CHECKPOINT_OK;}
