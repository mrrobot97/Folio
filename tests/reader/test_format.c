/* SPDX-License-Identifier: Apache-2.0
 * 真实模型/事务/文件层故障注入。/ Real model/transaction/file-layer fault injection.
 */
#include "format_model.h"
#include "format_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
static bool space_known=true;
static uint64_t space_bytes=4U*1024U*1024U;
static bool space_query(uint64_t* bytes,void* context){(void)context;*bytes=space_bytes;return space_known;}
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static char root[320];
static unsigned serial;
static void directory(char out[384]){snprintf(out,384,"%s/case%u",root,++serial);CHECK(mkdir(out,0700)==0);}
typedef enum {FAULT_NONE,FAULT_FSYNC,FAULT_CLOSE,FAULT_READBACK,FAULT_FINALIZE,FAULT_J_HEADER,FAULT_J_BODY,FAULT_J_FLUSH,FAULT_J_CLOSE,FAULT_J_READBACK} fault_t;
static fault_t fault;
static bool fired,readback_pending;
static long workspace_fail_after=-1;
static bool workspace_fail_after_prepared;
void* folio_format_fault_calloc(size_t count,size_t size){if(workspace_fail_after==0){errno=ENOMEM;return NULL;}if(workspace_fail_after>0)--workspace_fail_after;return calloc(count,size);}
static char written_path[512];
static unsigned journal_writes;
typedef struct {FILE* f;int fd;bool record,journal,writing,closed_payload,closing_write;char path[512];} stream_t;
static stream_t streams[32];
static stream_t* stream_for(FILE* f){for(unsigned i=0;i<32;i++)if(streams[i].f==f)return &streams[i];return NULL;}
FILE* folio_fault_fopen(const char* path,const char* mode){
    if((fault==FAULT_READBACK||fault==FAULT_J_READBACK)&&!fired&&readback_pending&&mode[0]=='r'&&!strcmp(path,written_path)){fired=true;errno=EIO;return NULL;}
    FILE* f=fopen(path,mode);if(!f)return NULL;
    stream_t* item=NULL;for(unsigned i=0;i<32;i++)if(!streams[i].f){item=&streams[i];break;}CHECK(item!=NULL);
    const char* name=strrchr(path,'/');name=name?name+1:path;
    *item=(stream_t){.f=f,.fd=fileno(f),.record=(!strncmp(name,"format_",7)&&strlen(name)==25)||!strncmp(name,"format_defaults.",16),.journal=!strncmp(name,"format_tx_",10),.writing=mode[0]=='w'};
    if(item->journal&&item->writing)item->closing_write=++journal_writes==2;
    snprintf(item->path,sizeof(item->path),"%s",path);return f;
}
#ifdef __APPLE__
FILE* folio_fault_fopen_darwin(const char* path,const char* mode) __asm__("_folio_fault_fopen$DARWIN_EXTSN");
FILE* folio_fault_fopen_darwin(const char* path,const char* mode){return folio_fault_fopen(path,mode);}
#endif
size_t folio_fault_fwrite(const void* data,size_t size,size_t count,FILE* f){
    stream_t* item=stream_for(f);if(item&&item->journal&&size*count>2048)item->closed_payload=((const unsigned char*)data)[0]==2;
    if(item&&item->closing_write&&!fired&&((fault==FAULT_J_HEADER&&size*count==28)||(fault==FAULT_J_BODY&&size*count>2048))){fired=true;return fwrite(data,size,count/2,f);}
    return fwrite(data,size,count,f);
}
int folio_fault_fflush(FILE* f){stream_t* item=stream_for(f);int result=fflush(f);if(item&&item->closing_write&&fault==FAULT_J_FLUSH&&!fired){fired=true;errno=EIO;return EOF;}return result;}
int folio_fault_fsync(int fd){
    stream_t* item=NULL;for(unsigned i=0;i<32;i++)if(streams[i].f&&streams[i].fd==fd){item=&streams[i];break;}
    if(!fired&&item&&item->writing&&((fault==FAULT_FSYNC&&item->record)||(fault==FAULT_FINALIZE&&item->journal&&item->closed_payload))){fired=true;errno=EIO;return -1;}
    return fsync(fd);
}
int folio_fault_fclose(FILE* f){
    stream_t* item=stream_for(f);bool record=item&&item->record&&item->writing;
    bool closing=item&&item->closing_write;
    bool prepared=item&&item->journal&&item->writing&&!item->closed_payload;
    if((record&&fault==FAULT_READBACK)||(closing&&fault==FAULT_J_READBACK)){snprintf(written_path,sizeof(written_path),"%s",item->path);readback_pending=true;}
    int result=fclose(f);if(item)memset(item,0,sizeof(*item));
    if(prepared&&workspace_fail_after_prepared){workspace_fail_after=0;workspace_fail_after_prepared=false;}
    if(((record&&fault==FAULT_CLOSE)||(closing&&fault==FAULT_J_CLOSE))&&!fired){fired=true;errno=EIO;return EOF;}return result;
}
static void inject(fault_t kind){fault=kind;fired=false;readback_pending=false;written_path[0]=0;journal_writes=0;}
static folio_format_snapshot_t fresh(const char* path){
    folio_format_snapshot_t s={0};s.scope=FORMAT_BOOK;s.route=FORMAT_ROUTE_READER;s.lifecycle=FORMAT_CLOSED;
    snprintf(s.book_path,sizeof(s.book_path),"%s",path);s.book_size=1234;s.book_mtime=100;
    s.active=s.root=s.field_entry=s.candidate=folio_format_builtin_defaults();s.active_revision=1;s.font_transfer_stopped=true;return s;
}
static void edit_size(folio_format_snapshot_t* s,int px){
    CHECK(folio_format_model_field_begin(s,FORMAT_FIELD_SIZE_WEIGHT));folio_format_values_t v=s->candidate;v.px=px;
    CHECK(folio_format_model_candidate(s,&v));CHECK(folio_format_model_field_keep(s));
}
static folio_format_snapshot_t applied(const folio_format_snapshot_t* live){folio_format_snapshot_t s=*live;s.active=s.root;s.field_entry=s.candidate=s.active;++s.active_revision;s.lifecycle=FORMAT_APPLIED;s.root_dirty=s.field_dirty=false;return s;}
static void test_model(void){
    folio_format_snapshot_t s=fresh("/a.txt");CHECK(folio_format_model_begin(&s));
    edit_size(&s,40);CHECK(s.active.px==44&&s.root.px==40);
    CHECK(folio_format_model_field_begin(&s,FORMAT_FIELD_SPACING));folio_format_values_t v=s.candidate;
    v.spacing=FORMAT_LOOSE;v.margin=FORMAT_MARGIN_WIDE;v.indent_em=0;CHECK(folio_format_model_candidate(&s,&v));
    CHECK(s.root.spacing==FORMAT_STANDARD);CHECK(folio_format_model_field_cancel(&s));CHECK(s.root.px==40&&s.root.margin==FORMAT_MARGIN_STANDARD);
    CHECK(folio_format_model_field_begin(&s,FORMAT_FIELD_BACKGROUND));v=s.candidate;v.background=FORMAT_PAPER_BLACK;v.px=48;CHECK(!folio_format_model_candidate(&s,&v));v.px=40;CHECK(folio_format_model_candidate(&s,&v));CHECK(folio_format_model_field_keep(&s));
    CHECK(s.active.background==FORMAT_PAPER_WHITE&&s.root.background==FORMAT_PAPER_BLACK);
    CHECK(folio_format_line_height(&s.active,44)==64&&folio_format_margin_pixels(&s.active)==40);
    CHECK(folio_format_paper(&s.root)==1&&folio_format_ink(&s.root)==15);
}
static void test_uncertain(fault_t kind,int action){
    char dir[384];directory(dir);inject(FAULT_NONE);folio_format_snapshot_t live=fresh("/a.txt");CHECK(folio_format_store_save(dir,&live));CHECK(folio_format_model_begin(&live));edit_size(&live,48);
    folio_format_snapshot_t attempt=applied(&live);inject(kind);CHECK(!folio_format_store_transition(dir,&live,&attempt));CHECK(fired&&folio_format_store_status()==FORMAT_STORE_UNKNOWN);
    CHECK(live.active.px==44&&live.root.px==48);
    folio_format_snapshot_t recovered;CHECK(folio_format_store_recover_pending(dir,&recovered)==FOLIO_CHECKPOINT_OK);
    CHECK(recovered.commit_unknown&&recovered.active.px==44&&recovered.root.px==48);
    // 未确认写后继续编辑，再 Apply/Discard/Suspend，都只沿已知前驱协调。
    // After an unknown write, changed Apply/Discard/Suspend must reconcile only the known predecessor.
    edit_size(&live,56);inject(FAULT_NONE);
    if(action==0){attempt=applied(&live);CHECK(folio_format_store_transition(dir,&live,&attempt));CHECK(folio_format_store_load(dir,FORMAT_BOOK,"/a.txt",&recovered)==FOLIO_CHECKPOINT_OK);CHECK(recovered.active.px==56&&recovered.lifecycle==FORMAT_APPLIED);}
    else if(action==1){attempt=live;attempt.lifecycle=FORMAT_DISCARDED;attempt.root=attempt.field_entry=attempt.candidate=attempt.active;CHECK(folio_format_store_transition(dir,&live,&attempt));CHECK(folio_format_store_load(dir,FORMAT_BOOK,"/a.txt",&recovered)==FOLIO_CHECKPOINT_OK);CHECK(recovered.active.px==44&&recovered.lifecycle==FORMAT_DISCARDED);}
    else{CHECK(folio_format_store_suspend(dir,&live));CHECK(folio_format_store_resume(dir,&recovered)==FOLIO_CHECKPOINT_OK);CHECK(recovered.active.px==44&&recovered.root.px==56&&recovered.lifecycle==FORMAT_EDITING);}
}
static void test_finalize(void){
    char dir[384];directory(dir);inject(FAULT_NONE);folio_format_snapshot_t live=fresh("/b.txt");CHECK(folio_format_store_save(dir,&live));CHECK(folio_format_model_begin(&live));edit_size(&live,52);
    folio_format_snapshot_t target=applied(&live);inject(FAULT_FINALIZE);CHECK(!folio_format_store_transition(dir,&live,&target));CHECK(fired&&folio_format_store_status()==FORMAT_STORE_COMMITTED_NEEDS_FINALIZE);CHECK(live.active.px==44);
    CHECK(!folio_format_store_finalize(dir));
    inject(FAULT_NONE);CHECK(folio_format_store_transition(dir,&live,&target));CHECK(folio_format_store_status()==FORMAT_STORE_OK);
    folio_format_snapshot_t read;CHECK(folio_format_store_load(dir,FORMAT_BOOK,"/b.txt",&read)==FOLIO_CHECKPOINT_OK&&read.active.px==52);
    // 陈旧且内容不同的写入仍不能覆盖。/ A stale different payload must still be rejected.
    inject(FAULT_NONE);live.root.px=60;target=applied(&live);CHECK(!folio_format_store_transition(dir,&live,&target));
}
static void test_closed_cold_boot(fault_t kind){
    char dir[384];directory(dir);folio_format_store_reset_runtime();inject(FAULT_NONE);folio_format_snapshot_t live=fresh("/cold.txt");
    CHECK(folio_format_store_save(dir,&live));CHECK(folio_format_model_begin(&live));edit_size(&live,60);
    folio_format_snapshot_t target=applied(&live);inject(kind);CHECK(!folio_format_store_transition(dir,&live,&target));CHECK(fired);
    CHECK(live.active.px==44&&live.root.px==60);
    // 丢弃所有进程内意图凭据后按真实磁盘重新解释，不能用旧RAM冒充重启。
    // Drop every in-process intent credential and interpret real files, rather than pretending old RAM survived reboot.
    folio_format_store_reset_runtime();inject(FAULT_NONE);folio_format_snapshot_t loaded;
    CHECK(folio_format_store_load(dir,FORMAT_BOOK,"/cold.txt",&loaded)==FOLIO_CHECKPOINT_OK);
    if(kind==FAULT_J_HEADER||kind==FAULT_J_BODY){CHECK(loaded.commit_unknown&&loaded.active.px==44&&loaded.root.px==60);CHECK(folio_format_store_recover_pending(dir,&loaded)==FOLIO_CHECKPOINT_OK);}
    else{CHECK(!loaded.commit_unknown&&loaded.active.px==60&&loaded.lifecycle==FORMAT_APPLIED);CHECK(folio_format_store_recover_pending(dir,&loaded)==FOLIO_CHECKPOINT_MISSING);}
}
static void test_workspaces(void){
    char dir[384];directory(dir);folio_format_store_reset_runtime();inject(FAULT_NONE);folio_format_snapshot_t live=fresh("/oom.txt");CHECK(folio_format_store_save(dir,&live));
    folio_format_snapshot_t out,before;memset(&out,0xa5,sizeof(out));before=out;
    workspace_fail_after=0;CHECK(folio_format_store_raw_load(dir,FORMAT_BOOK,"/oom.txt",&out)==FOLIO_CHECKPOINT_IO);CHECK(!memcmp(&out,&before,sizeof(out)));
    workspace_fail_after=1;CHECK(folio_format_store_raw_load(dir,FORMAT_BOOK,"/oom.txt",&out)==FOLIO_CHECKPOINT_IO);CHECK(!memcmp(&out,&before,sizeof(out)));workspace_fail_after=-1;
    CHECK(folio_format_model_begin(&live));edit_size(&live,48);folio_format_snapshot_t target=applied(&live);
    workspace_fail_after_prepared=true;CHECK(!folio_format_store_transition(dir,&live,&target));CHECK(workspace_fail_after==0&&live.active.px==44);workspace_fail_after=-1;
    folio_format_store_reset_runtime();CHECK(folio_format_store_recover_pending(dir,&out)==FOLIO_CHECKPOINT_OK);CHECK(out.commit_unknown&&out.active.px==44&&out.root.px==48);
    target=applied(&out);CHECK(folio_format_store_transition(dir,&out,&target));CHECK(folio_format_store_load(dir,FORMAT_BOOK,"/oom.txt",&out)==FOLIO_CHECKPOINT_OK&&out.active.px==48);
}
static size_t files_in(const char* path){DIR*d=opendir(path);CHECK(d);size_t n=0;struct dirent*e;while((e=readdir(d)))if(e->d_name[0]!='.')n++;CHECK(!closedir(d));return n;}
static void test_space_preflight(void){
    char dir[384];directory(dir);folio_format_store_reset_runtime();inject(FAULT_NONE);folio_format_snapshot_t s=fresh("/space.txt");
    space_known=false;CHECK(!folio_format_store_seed_baseline(dir,&s));CHECK(folio_format_store_status()==FORMAT_STORE_NO_SPACE&&files_in(dir)==0);
    space_known=true;space_bytes=FOLIO_FORMAT_BASELINE_RESERVE-1;CHECK(!folio_format_store_seed_baseline(dir,&s));CHECK(files_in(dir)==0);
    space_bytes=FOLIO_FORMAT_BASELINE_RESERVE;CHECK(folio_format_store_seed_baseline(dir,&s));CHECK(files_in(dir)==1);
    CHECK(folio_format_model_begin(&s));edit_size(&s,48);folio_format_snapshot_t target=applied(&s);
    space_bytes=FOLIO_FORMAT_TRANSACTION_RESERVE-1;CHECK(!folio_format_store_transition(dir,&s,&target));CHECK(folio_format_store_status()==FORMAT_STORE_NO_SPACE&&files_in(dir)==1);
    folio_format_snapshot_t read;CHECK(folio_format_store_load(dir,FORMAT_BOOK,"/space.txt",&read)==FOLIO_CHECKPOINT_OK&&read.active.px==44);
    space_bytes=FOLIO_FORMAT_REFERENCE_RESERVE-1;CHECK(!folio_format_store_clear_resume(dir));CHECK(files_in(dir)==1);
    space_bytes=4U*1024U*1024U;
}
int main(void){folio_format_store_set_space_query(space_query,NULL);snprintf(root,sizeof(root),"/tmp/folio-format-XXXXXX");CHECK(mkdtemp(root));test_model();for(int kind=FAULT_FSYNC;kind<=FAULT_READBACK;kind++)for(int action=0;action<3;action++)test_uncertain(kind,action);test_finalize();test_closed_cold_boot(FAULT_FINALIZE);for(int kind=FAULT_J_HEADER;kind<=FAULT_J_READBACK;kind++)test_closed_cold_boot(kind);test_workspaces();test_space_preflight();printf("PASS format: %u checks, I/O faults, workspace OOM, cold-boot recovery and pre-write space checks; %s\n",checks,root);}
