/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 有界排版意图journal：确认基线、已知前驱和目标分离，不确定写入不能冒充应用。
 * Bounded format intent journals separate confirmed base, known predecessor and target; uncertain writes never imply activation.
 * 冻结：PREPARED恢复旧生效值和待确认草稿；CLOSED必须有完整R ACK前序，未知记录不覆盖。
 * Frozen: PREPARED restores old active values and pending draft; CLOSED follows a complete record ACK; preserve unknown records.
 */
#include "format_store.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "book_store.h"
#endif
#define JOURNAL_BYTES (3*FOLIO_FORMAT_WIRE_SIZE+64)
#define OWNER_BYTES 296
#define TX_PREPARED 1
#define TX_CLOSED 2
typedef struct {
    uint8_t state;
    bool predecessor_present;
    folio_format_snapshot_t base,predecessor,target;
} transaction_t;
static folio_format_store_status_t s_status;
static bool s_cleanup_pending;
static folio_format_snapshot_t s_confirmed;
static folio_format_snapshot_t s_unpublished_base;
static char s_unpublished_directory[320];
static folio_format_space_query_t s_space_query;
static void* s_space_context;
void folio_format_store_set_space_query(folio_format_space_query_t query,void* context){s_space_query=query;s_space_context=context;}
bool folio_format_store_require_space(uint64_t required){
    uint64_t available=0;bool known=false;
    if(s_space_query)known=s_space_query(&available,s_space_context);
#ifdef ESP_PLATFORM
    else if(book_store_flash_ready()){
        book_store_root_t root={.path="/flash/books",.is_flash=true};
        available=book_store_free_bytes(&root);known=available>0;
    }
#endif
    if(!known||available<required){s_status=FORMAT_STORE_NO_SPACE;return false;}
    return true;
}
static void* allocate(size_t n){
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return calloc(1,n);
#endif
}
static void put(unsigned char** p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)*(*p)++=(unsigned char)(v>>(8*i));}
static uint64_t get(const unsigned char** p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)*(*p)++<<(8*i);return v;}
static bool same_owner(const folio_format_snapshot_t* a,const folio_format_snapshot_t* b){return a->scope==b->scope&&!strcmp(a->book_path,b->book_path)&&a->book_size==b->book_size&&a->book_mtime==b->book_mtime;}
static bool same_payload(const folio_format_snapshot_t* a,const folio_format_snapshot_t* b){
    unsigned char* data=allocate(FOLIO_FORMAT_WIRE_SIZE*2);if(!data)return false;
    folio_format_store_encode(a,data);folio_format_store_encode(b,data+FOLIO_FORMAT_WIRE_SIZE);
    bool same=!memcmp(data,data+FOLIO_FORMAT_WIRE_SIZE,FOLIO_FORMAT_WIRE_SIZE);free(data);return same;
}
static bool journal_name(folio_format_scope_t scope,const char* path,char name[40]){
    if(scope==FORMAT_DEFAULTS){snprintf(name,40,"format_tx_defaults");return !path||!path[0];}
    if(scope!=FORMAT_BOOK||!path||!path[0]||strlen(path)>=FOLIO_FORMAT_BOOK_PATH_MAX)return false;
    uint64_t hash=UINT64_C(14695981039346656037);for(const unsigned char* p=(const unsigned char*)path;*p;p++)hash=(hash^*p)*UINT64_C(1099511628211);
    snprintf(name,40,"format_tx_%016llx",(unsigned long long)hash);return true;
}
static int book_quota_admits(const char* directory,const folio_format_snapshot_t* target){
    if(target->scope==FORMAT_DEFAULTS)return 1;
    char target_name[40];if(!journal_name(target->scope,target->book_path,target_name))return -1;
    uint64_t requested=0;for(unsigned i=10;i<26;i++){char c=target_name[i];requested=(requested<<4)|(uint64_t)(c<='9'?c-'0':c-'a'+10);}
    uint64_t hashes[128];size_t count=0;bool exists=false,overflow=false;
    DIR* dir=opendir(directory);if(!dir)return -1;struct dirent* entry;
    while((entry=readdir(dir))){
        const char* name=entry->d_name;size_t len=strlen(name);const char* hex=NULL;
        if(len==25&&!strncmp(name,"format_",7)&&name[23]=='.'&&(name[24]=='a'||name[24]=='b'))hex=name+7;
        if(len==28&&!strncmp(name,"format_tx_",10)&&name[26]=='.'&&(name[27]=='a'||name[27]=='b'))hex=name+10;
        if(!hex)continue;
        uint64_t hash=0;bool valid=true;
        for(unsigned i=0;i<16;i++){char c=hex[i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))){valid=false;break;}hash=(hash<<4)|(uint64_t)(c<='9'?c-'0':c-'a'+10);}
        if(!valid)continue;
        if(hash==requested)exists=true;
        bool seen=false;for(size_t i=0;i<count;i++)if(hashes[i]==hash){seen=true;break;}
        if(!seen){if(count==128)overflow=true;else hashes[count++]=hash;}
    }
    bool closed=closedir(dir)==0;
    // 记录和仅journal占位统一计额；半成品也保留，不靠擦除腾空间。
    // Count record and journal-only reservations together, retaining partial files rather than reclaiming them.
    return !closed?-1:(exists||(!overflow&&count<128))?1:0;
}
static bool write_journal(const char* directory,const transaction_t* tx){
    char name[40];if(!journal_name(tx->target.scope,tx->target.book_path,name))return false;
    unsigned char* data=allocate(JOURNAL_BYTES);if(!data)return false;unsigned char* p=data;
    put(&p,tx->state,1);put(&p,tx->predecessor_present,1);put(&p,tx->base.generation,8);put(&p,tx->predecessor.generation,8);put(&p,tx->target.generation,8);
    folio_format_store_encode(&tx->base,p);p+=FOLIO_FORMAT_WIRE_SIZE;
    folio_format_store_encode(&tx->predecessor,p);p+=FOLIO_FORMAT_WIRE_SIZE;
    folio_format_store_encode(&tx->target,p);
    uint64_t generation=0;bool ok=folio_checkpoint_save(directory,name,2,data,JOURNAL_BYTES,&generation)==FOLIO_CHECKPOINT_OK;free(data);return ok;
}
static folio_checkpoint_result_t read_journal(const char* directory,folio_format_scope_t scope,const char* path,transaction_t* tx){
    char name[40];if(!journal_name(scope,path,name))return FOLIO_CHECKPOINT_IO;
    unsigned char* data=allocate(JOURNAL_BYTES);if(!data)return FOLIO_CHECKPOINT_IO;size_t len=0;uint64_t generation=0;
    folio_checkpoint_result_t result=folio_checkpoint_load(directory,name,2,data,JOURNAL_BYTES,&len,&generation);
    if(result!=FOLIO_CHECKPOINT_OK){free(data);return result;}
    const unsigned char* p=data;unsigned state=get(&p,1),present=get(&p,1);
    uint64_t base_gen=get(&p,8),pred_gen=get(&p,8),target_gen=get(&p,8);
    bool valid=len==JOURNAL_BYTES&&(state==TX_PREPARED||state==TX_CLOSED)&&present<2;
    if(valid){valid=folio_format_store_decode(p,&tx->base);p+=FOLIO_FORMAT_WIRE_SIZE;}
    if(valid){valid=folio_format_store_decode(p,&tx->predecessor);p+=FOLIO_FORMAT_WIRE_SIZE;}
    if(valid){valid=folio_format_store_decode(p,&tx->target);p+=FOLIO_FORMAT_WIRE_SIZE;}
    if(valid)while(p<data+JOURNAL_BYTES)if(*p++){valid=false;break;}
    free(data);if(!valid)return FOLIO_CHECKPOINT_CORRUPT;
    tx->state=state;tx->predecessor_present=present;tx->base.generation=base_gen;tx->predecessor.generation=pred_gen;tx->target.generation=target_gen;
    if(tx->target.scope!=scope||strcmp(tx->target.book_path,path?path:"")||!same_owner(&tx->base,&tx->target)||!same_owner(&tx->predecessor,&tx->target))return FOLIO_CHECKPOINT_CORRUPT;
    return FOLIO_CHECKPOINT_OK;
}
static bool observed_is_known(const transaction_t* tx,folio_checkpoint_result_t result,const folio_format_snapshot_t* raw){
    if(result==FOLIO_CHECKPOINT_MISSING)return !tx->predecessor_present;
    if(result!=FOLIO_CHECKPOINT_OK)return false;
    return (tx->predecessor_present&&raw->generation==tx->predecessor.generation&&same_payload(raw,&tx->predecessor))||
        (raw->generation>tx->predecessor.generation&&same_payload(raw,&tx->target));
}
static bool write_owner(const char* directory,const folio_format_snapshot_t* s){
    unsigned char ref[OWNER_BYTES]={0};ref[0]=s->scope;memcpy(ref+1,s->book_path,sizeof(s->book_path));uint64_t generation=0;
    return folio_checkpoint_save(directory,"format_pending_owner",1,ref,sizeof(ref),&generation)==FOLIO_CHECKPOINT_OK;
}
static folio_checkpoint_result_t read_owner(const char* directory,folio_format_scope_t* scope,char path[FOLIO_FORMAT_BOOK_PATH_MAX]){
    unsigned char ref[OWNER_BYTES];size_t len=0;uint64_t generation=0;folio_checkpoint_result_t result=folio_checkpoint_load(directory,"format_pending_owner",1,ref,sizeof(ref),&len,&generation);
    if(result!=FOLIO_CHECKPOINT_OK)return result;
    if(len!=sizeof(ref)||!memchr(ref+1,0,FOLIO_FORMAT_BOOK_PATH_MAX))return FOLIO_CHECKPOINT_CORRUPT;
    *scope=ref[0];memcpy(path,ref+1,FOLIO_FORMAT_BOOK_PATH_MAX);if(*scope!=FORMAT_BOOK&&*scope!=FORMAT_DEFAULTS)return FOLIO_CHECKPOINT_CORRUPT;return FOLIO_CHECKPOINT_OK;
}
folio_format_store_status_t folio_format_store_status(void){return s_status;}
void folio_format_store_reset_runtime(void){s_status=FORMAT_STORE_OK;s_cleanup_pending=false;memset(&s_confirmed,0,sizeof(s_confirmed));memset(&s_unpublished_base,0,sizeof(s_unpublished_base));s_unpublished_directory[0]=0;}
bool folio_format_store_finalize(const char* directory){
    (void)directory;
    // 未发布的最终确认必须由带完整 live intent 的重试协调，不能在睡前偷偷完成旧 Apply。
    // Unpublished confirmation requires a retry with the complete live intent; never finish an old Apply implicitly at sleep.
    if(s_cleanup_pending){s_status=FORMAT_STORE_COMMITTED_NEEDS_FINALIZE;return false;}
    return true;

}
folio_checkpoint_result_t folio_format_store_load(const char* directory,folio_format_scope_t scope,const char* path,folio_format_snapshot_t* out){
    folio_format_snapshot_t raw;folio_checkpoint_result_t result=folio_format_store_raw_load(directory,scope,path,&raw);
    if(result!=FOLIO_CHECKPOINT_OK)return result;
    transaction_t* tx=allocate(sizeof(*tx));if(!tx)return FOLIO_CHECKPOINT_IO;
    result=read_journal(directory,scope,path,tx);
    if(result==FOLIO_CHECKPOINT_MISSING&&!raw.transaction_id){free(tx);*out=raw;return FOLIO_CHECKPOINT_OK;}
    if(result==FOLIO_CHECKPOINT_OK&&tx->state==TX_CLOSED){
        if(raw.generation!=tx->target.generation||!same_payload(&raw,&tx->target))result=FOLIO_CHECKPOINT_CORRUPT;
        else *out=raw;
    }else if(result==FOLIO_CHECKPOINT_OK){
        if(!observed_is_known(tx,FOLIO_CHECKPOINT_OK,&raw))result=FOLIO_CHECKPOINT_CORRUPT;
        else{
            *out=tx->target.lifecycle==FORMAT_DISCARDED?tx->base:tx->target;
            out->active=tx->base.active;out->active_revision=tx->base.active_revision;out->transaction_id=tx->base.transaction_id;
            out->generation=raw.generation;out->lifecycle=FORMAT_EDITING;out->commit_unknown=true;out->recovered_checkpoint=true;
            out->root_dirty=!folio_format_values_equal(&out->root,&out->active);
        }
    }else if(result==FOLIO_CHECKPOINT_MISSING)result=FOLIO_CHECKPOINT_CORRUPT;
    free(tx);return result;
}
folio_checkpoint_result_t folio_format_store_recover_pending(const char* directory,folio_format_snapshot_t* out){
    folio_format_scope_t scope;char path[FOLIO_FORMAT_BOOK_PATH_MAX];folio_checkpoint_result_t result=read_owner(directory,&scope,path);
    if(result!=FOLIO_CHECKPOINT_OK)return result;
    transaction_t* tx=allocate(sizeof(*tx));if(!tx)return FOLIO_CHECKPOINT_IO;
    result=read_journal(directory,scope,path,tx);bool pending=result==FOLIO_CHECKPOINT_OK&&tx->state==TX_PREPARED&&tx->base.session_id;
    free(tx);if(result!=FOLIO_CHECKPOINT_OK)return result;if(!pending)return FOLIO_CHECKPOINT_MISSING;
    return folio_format_store_load(directory,scope,path,out);
}
bool folio_format_store_transition(const char* directory,const folio_format_snapshot_t* live,folio_format_snapshot_t* target){
    s_status=FORMAT_STORE_FAILED;if(!live||!target||!same_owner(live,target))return false;
    // 覆盖 owner、两个 journal 槽、一个记录槽与保守目录余量；实际 I/O 失败仍按未知事务处理。
    // Cover owner, two journal slots, one record slot and metadata margin; actual I/O failure still uses uncertain-write recovery.
    if(!folio_format_store_require_space(FOLIO_FORMAT_TRANSACTION_RESERVE))return false;
    int admitted=book_quota_admits(directory,target);if(admitted!=1){s_status=admitted==0?FORMAT_STORE_CAPACITY:FORMAT_STORE_FAILED;return false;}
    transaction_t* tx=allocate(sizeof(*tx));transaction_t* old=allocate(sizeof(*old));folio_format_snapshot_t* raw=allocate(sizeof(*raw));
    if(!tx||!old||!raw){free(tx);free(old);free(raw);return false;}
    folio_checkpoint_result_t observed=folio_format_store_raw_load(directory,target->scope,target->book_path,raw);
    folio_checkpoint_result_t journal=read_journal(directory,target->scope,target->book_path,old);
    bool ok=observed==FOLIO_CHECKPOINT_OK||observed==FOLIO_CHECKPOINT_MISSING;
    folio_format_scope_t pending_scope;char pending_path[FOLIO_FORMAT_BOOK_PATH_MAX];
    folio_checkpoint_result_t owner=read_owner(directory,&pending_scope,pending_path);
    if(owner!=FOLIO_CHECKPOINT_OK&&owner!=FOLIO_CHECKPOINT_MISSING)ok=false;
    if(owner==FOLIO_CHECKPOINT_OK&&(pending_scope!=target->scope||strcmp(pending_path,target->book_path))){
        transaction_t* pending=allocate(sizeof(*pending));
        if(!pending)ok=false;
        else{folio_checkpoint_result_t state=read_journal(directory,pending_scope,pending_path,pending);if(state!=FOLIO_CHECKPOINT_OK||pending->state!=TX_CLOSED)ok=false;free(pending);}
    }
    if(journal!=FOLIO_CHECKPOINT_OK&&journal!=FOLIO_CHECKPOINT_MISSING)ok=false;
    bool unpublished=s_cleanup_pending&&!strcmp(directory,s_unpublished_directory)&&same_owner(live,&s_unpublished_base)&&
        live->session_id==s_unpublished_base.session_id&&live->active_revision==s_unpublished_base.active_revision&&
        folio_format_values_equal(&live->active,&s_unpublished_base.active)&&observed==FOLIO_CHECKPOINT_OK&&
        raw->generation==s_confirmed.generation&&same_payload(raw,&s_confirmed);
    if(s_cleanup_pending&&!unpublished)ok=false;
    if(ok&&unpublished){
        // 同进程仍未向 UI 发布：只协调本次已知完整目标，允许用户修改后 Apply/Discard/Suspend。
        // Still unpublished in this run: reconcile only the known full target, allowing changed Apply/Discard/Suspend intent.
    }else if(ok&&journal==FOLIO_CHECKPOINT_OK&&old->state==TX_PREPARED){
        ok=observed_is_known(old,observed,raw)&&live->session_id==old->target.session_id&&
            live->active_revision==old->base.active_revision&&folio_format_values_equal(&live->active,&old->base.active);
    }else if(ok&&observed==FOLIO_CHECKPOINT_OK){
        ok=target->generation==raw->generation&&(!raw->transaction_id||(journal==FOLIO_CHECKPOINT_OK&&old->state==TX_CLOSED&&same_payload(raw,&old->target)));
    }else if(ok&&target->generation)ok=false;
    if(!ok)goto done;
    tx->base=*live;tx->base.intent_attempted=true;tx->predecessor_present=observed==FOLIO_CHECKPOINT_OK;
    tx->predecessor=tx->predecessor_present?*raw:*live;
    if(!tx->predecessor_present)tx->predecessor.generation=0;
    tx->target=*target;tx->target.intent_attempted=true;tx->target.generation=tx->predecessor.generation;
    uint64_t transaction=tx->predecessor.transaction_id;
    if(journal==FOLIO_CHECKPOINT_OK&&old->target.transaction_id>transaction)transaction=old->target.transaction_id;
    if(transaction==UINT64_MAX){ok=false;goto done;}
    tx->target.transaction_id=transaction+1;tx->state=TX_PREPARED;
    if(!write_owner(directory,&tx->target)||!write_journal(directory,tx)){s_status=FORMAT_STORE_UNKNOWN;ok=false;goto done;}
    s_cleanup_pending=false;
    if(!folio_format_store_raw_save(directory,&tx->target)){s_status=FORMAT_STORE_UNKNOWN;ok=false;goto done;}
    // R ACK 仅完成内部准备；只有 CLOSED 完整 ACK 后向调用方发布。
    // Record ACK completes internal preparation; publish to the caller only after complete CLOSED ACK.
    s_confirmed=tx->target;s_unpublished_base=*live;s_cleanup_pending=true;
    snprintf(s_unpublished_directory,sizeof(s_unpublished_directory),"%s",directory);
    tx->state=TX_CLOSED;
    if(write_journal(directory,tx)){s_cleanup_pending=false;s_status=FORMAT_STORE_OK;*target=tx->target;ok=true;}
    else{s_status=FORMAT_STORE_COMMITTED_NEEDS_FINALIZE;ok=false;}
done:
    free(tx);free(old);free(raw);return ok;
}
bool folio_format_store_save(const char* directory,folio_format_snapshot_t* snapshot){
    if(!snapshot)return false;
    folio_format_snapshot_t* live=allocate(sizeof(*live));if(!live)return false;
    *live=*snapshot;bool ok=folio_format_store_transition(directory,live,snapshot);free(live);return ok;
}
bool folio_format_store_seed_baseline(const char* directory,folio_format_snapshot_t* snapshot){
    s_status=FORMAT_STORE_FAILED;
    if(!snapshot||snapshot->scope!=FORMAT_BOOK||snapshot->lifecycle!=FORMAT_CLOSED||snapshot->session_id||snapshot->revision||snapshot->transaction_id||
       snapshot->field!=FORMAT_FIELD_NONE||snapshot->font_child||!folio_format_values_equal(&snapshot->active,&snapshot->root)||
       !folio_format_values_equal(&snapshot->active,&snapshot->candidate)||!folio_format_values_equal(&snapshot->active,&snapshot->field_entry))return false;
    if(!folio_format_store_require_space(FOLIO_FORMAT_BASELINE_RESERVE))return false;
    int admitted=book_quota_admits(directory,snapshot);if(admitted!=1){s_status=admitted==0?FORMAT_STORE_CAPACITY:FORMAT_STORE_FAILED;return false;}
    folio_format_snapshot_t existing;
    folio_checkpoint_result_t observed=folio_format_store_raw_load(directory,snapshot->scope,snapshot->book_path,&existing);
    if(observed!=FOLIO_CHECKPOINT_OK&&observed!=FOLIO_CHECKPOINT_MISSING)return false;
    if(observed==FOLIO_CHECKPOINT_MISSING&&snapshot->generation)return false;
    if(observed==FOLIO_CHECKPOINT_OK&&(!same_payload(&existing,snapshot)||existing.transaction_id||existing.session_id||existing.lifecycle!=FORMAT_CLOSED))return false;
    transaction_t* tx=allocate(sizeof(*tx));if(!tx)return false;
    folio_checkpoint_result_t result=read_journal(directory,snapshot->scope,snapshot->book_path,tx);free(tx);
    if(result!=FOLIO_CHECKPOINT_MISSING)return false;
    if(observed==FOLIO_CHECKPOINT_OK)snapshot->generation=existing.generation;
    bool ok=folio_format_store_raw_save(directory,snapshot);s_status=ok?FORMAT_STORE_OK:FORMAT_STORE_FAILED;return ok;
}
