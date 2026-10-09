/* SPDX-License-Identifier: Apache-2.0
 * 真实服务/存储/字体/分页/reader适配及R01/R04渲染；平台和已解析书源为fixture。
 * Real service/store/font/pagination/reader adapters and R01/R04 rendering; platform and parsed book sources are fixtures.
 */
#define FOLIO_UI_REAL_FONT
#include "../ui/ui_host_test.c"
#include "format_store.h"
#include "reader_draft.h"
#include "book_layout.h"
#include "book_progress.h"
#include "transfer_navigation.h"
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
static int fail_alloc_after=-1;
static char default_font[160]="builtin";
static int workers_stopped,progress_saves;
static bool space_query(uint64_t* bytes,void* context){(void)context;*bytes=4U*1024U*1024U;return true;}
void host_log(const char* tag,const char* text,...){(void)tag;(void)text;}
void*heap_caps_malloc(size_t n,unsigned caps){(void)caps;if(fail_alloc_after==0)return NULL;if(fail_alloc_after>0)--fail_alloc_after;return malloc(n);}
void*heap_caps_calloc(size_t n,size_t size,unsigned caps){void*p=heap_caps_malloc(n*size,caps);if(p)memset(p,0,n*size);return p;}
void*heap_caps_realloc(void*p,size_t n,unsigned caps){(void)caps;if(fail_alloc_after==0)return NULL;if(fail_alloc_after>0)--fail_alloc_after;return realloc(p,n);}
void heap_caps_free(void*p){free(p);}
size_t heap_caps_get_largest_free_block(unsigned caps){(void)caps;return 8u*1024u*1024u;}
int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
const char*app_settings_font_path(void){return default_font;}
void app_present(app_ctx_t*ctx,const app_desc_t*app,app_redraw_t redraw){if(redraw!=APP_REDRAW_NONE)app->render(ctx,ctx->fb);}
void app_transfer_open_font_child(app_ctx_t*ctx,const app_desc_t*page){(void)page;ctx->request_app=&app_transfer;}
bool app_transfer_is_font_child(void){return folio_format_snapshot()->font_child;}
static char chapter_text[12000];
static char* s_text;
static size_t s_text_len,s_page,s_chapter,s_block_count,s_format_anchor;
static blk_t* s_blocks;
static char s_path[288],s_title[128]="风经过窗前",s_book_title[128]="山间来信",s_font_path[192];
static uint32_t s_file_size;
static int64_t s_book_mtime;
static int s_px;
static bool s_formatted,s_format_retained,s_toolbar,s_save_failed,s_stats_failed,s_draft_pending,s_draft_failed;
static const char* s_blocked_reason="";
typedef struct{void* watch;} pending_progress_t;
static bool fixture_invalidated;
static pending_progress_t fixture_progress={.watch=&fixture_invalidated};
static pending_progress_t* pending_find(const char* path){(void)path;return &fixture_progress;}
bool book_progress_watch_invalidated(const book_progress_watch_t*watch){return *(const bool*)watch;}
static folio_format_values_t s_active_format,s_prepared_format;
static uint64_t s_active_format_revision;
static folio_reader_draft_t s_draft;
bool folio_reader_has_unapplied_draft(void){return folio_format_has_session()||s_draft.editing;}
static book_layout_candidate_t* s_format_layout;
static void* s_prep_task,*s_prep_done,*s_next_fb;
static enum{SHELF,READING} s_view;
static enum{ENTRY_DEFAULT} s_entry;
extern const app_desc_t app_book;
static void copy_text(char* out,size_t cap,const char* in){snprintf(out,cap,"%s",in);}
static void lock_draw(void){}
static void unlock_draw(void){}
static void invalidate_prep(void){}
int ui_content_width(void){return 604;}
static void save_progress(void){progress_saves++;}
static void vTaskDelete(void*p){assert(p);workers_stopped++;}
static void vSemaphoreDelete(void*p){(void)p;}
static void free_book(void){book_layout_free();s_text=NULL;s_path[0]=0;s_formatted=false;}
static unsigned percent(size_t page){return s_text_len?(unsigned)(book_layout_page_start_offset(page)*100/s_text_len):0;}
static bool open_book(app_ctx_t*ctx,const char*path);
static bool load_book_for_format(app_ctx_t*ctx,const folio_format_snapshot_t* session){
    (void)ctx;struct stat st;if(stat(session->book_path,&st)||(uint64_t)st.st_size!=session->book_size||st.st_mtime!=session->book_mtime)return false;
    book_layout_free();strcpy(s_path,session->book_path);s_file_size=st.st_size;s_book_mtime=st.st_mtime;s_text=chapter_text;s_text_len=strlen(chapter_text);
    s_active_format=session->active;s_active_format_revision=session->active_revision;s_px=session->active.px;s_formatted=true;s_page=s_chapter=s_format_anchor=0;return true;
}
#include "reader_production.inc"
static void render_book(app_ctx_t*ctx,uint8_t*buffer){(void)ctx;draw_formatted_reader(buffer,s_page);}
const app_desc_t app_book={.title="阅读",.render=render_book};
static folio_format_font_t font_identity(const char* path){ttf_font_identity_t raw;assert(ttf_font_identity_read(path,&raw)==ESP_OK);folio_format_font_t id={0};snprintf(id.path,sizeof(id.path),"%s",raw.path);id.file_size=raw.file_size;id.mtime=raw.mtime;id.header_crc=raw.header_crc;return id;}
static bool open_book(app_ctx_t*ctx,const char*path){
    (void)ctx;struct stat st;if(stat(path,&st))return false;
    folio_format_values_t legacy=folio_format_builtin_defaults();snprintf(legacy.font.path,sizeof(legacy.font.path),"%s",default_font);
    if(!folio_format_resolve_book(path,st.st_size,st.st_mtime,false,BOOK_PROGRESS_NATIVE,&legacy,&s_active_format,&s_active_format_revision))return false;
    strcpy(s_path,path);s_file_size=st.st_size;s_book_mtime=st.st_mtime;fixture_invalidated=false;s_text=chapter_text;s_text_len=strlen(chapter_text);s_px=s_active_format.px;s_formatted=true;s_page=s_chapter=0;s_view=READING;
    if(ttf_font_prepare(TTF_ROLE_READER,s_active_format.font.path)!=ESP_OK)return false;
    ttf_font_scope_t font;assert(ttf_font_begin(TTF_ROLE_READER,&font)==ESP_OK);ttf_set_weight(folio_format_weight_number(&s_active_format));
    bool ok=book_layout_begin_formatted(s_text,s_text_len,NULL,0,body_rect(),&s_active_format);ttf_font_end(&font);return ok;
}
static void file_book(const char* path){FILE*f=fopen(path,"wb");assert(f);assert(fwrite(chapter_text,1,strlen(chapter_text),f)==strlen(chapter_text));assert(!fclose(f));}
static void capture_book(app_ctx_t*ctx,const char*out,const char*name){ttf_font_cache_stats_t before_font,after_font;ttf_font_cache_stats(TTF_ROLE_READER,&before_font);draw(&app_book,ctx,out,name);ttf_font_cache_stats(TTF_ROLE_READER,&after_font);assert(before_font.opens==after_font.opens);}
static void keep_size(int delta){assert(folio_format_field_begin(FORMAT_FIELD_SIZE_WEIGHT));while(delta){int step=delta>0?4:-4;assert(folio_format_size_delta(step));delta-=step;}assert(folio_format_field_keep());}
static size_t owned_books(void){
    DIR*dir=opendir(FORMAT_DIRECTORY);assert(dir);uint64_t ids[256];size_t count=0;struct dirent*entry;
    while((entry=readdir(dir))){const char*name=entry->d_name;size_t len=strlen(name);const char*hex=NULL;
        if(len==25&&!strncmp(name,"format_",7))hex=name+7;
        if(len==28&&!strncmp(name,"format_tx_",10))hex=name+10;
        if(!hex)continue;char*end;uint64_t id=strtoull(hex,&end,16);if(end-hex!=16||*end!='.')continue;
        bool seen=false;for(size_t i=0;i<count;i++)if(ids[i]==id)seen=true;if(!seen){assert(count<256);ids[count++]=id;}}
    assert(!closedir(dir));return count;
}
static size_t directory_files(void){DIR*dir=opendir(FORMAT_DIRECTORY);assert(dir);size_t count=0;struct dirent*entry;while((entry=readdir(dir)))if(entry->d_name[0]!='.')count++;assert(!closedir(dir));return count;}
int main(int argc,char**argv){
    assert(argc==3);folio_format_store_set_space_query(space_query,NULL);const char*full_font=argv[1],*out=argv[2];assert(mkdir(FORMAT_DIRECTORY,0700)==0||errno==EEXIST);
    const char* paragraph="清晨推开木窗，山风正从河谷缓缓吹来。昨夜的雨停了，石阶上还留着细小的水珠。\n我把读到一半的书放在桌上，没有急着翻页。窗外有人走过，脚步声穿过安静的小巷，又慢慢远了。\n在这样的早晨，时间好像也放轻了脚步。我们不必走得太快，也不必急着抵达。\n坐下来，读完这一页。\n";
    for(int i=0;i<6;i++)strcat(chapter_text,paragraph);
    assert(ttf_font_init()==ESP_OK);snprintf(default_font,sizeof(default_font),"%s",full_font);
    char book_a[288],book_b[288],book_c[288];snprintf(book_a,sizeof(book_a),"%s/a.txt",out);snprintf(book_b,sizeof(book_b),"%s/b.txt",out);snprintf(book_c,sizeof(book_c),"%s/c.txt",out);file_book(book_a);file_book(book_b);file_book(book_c);
    app_ctx_t ctx={.fb=fb};assert(open_book(&ctx,book_a));s_page=1;size_t anchor=book_layout_page_start_offset(s_page);
    capture_book(&ctx,out,"R01-before");memcpy(before,fb,sizeof(fb));assert(folio_format_begin_book(&ctx));app_folio_format.on_enter(&ctx);
    assert(folio_format_field_begin(FORMAT_FIELD_SIZE_WEIGHT));assert(folio_format_size_delta(4));assert(folio_format_set_weight(FORMAT_BOLD));assert(folio_format_field_keep());
    assert(folio_format_field_begin(FORMAT_FIELD_SPACING));assert(folio_format_set_spacing(FORMAT_LOOSE));assert(folio_format_set_margin(FORMAT_MARGIN_WIDE));assert(folio_format_set_indent(2));assert(folio_format_field_keep());
    assert(folio_format_field_begin(FORMAT_FIELD_BACKGROUND));assert(folio_format_set_background(FORMAT_PAPER_GRAY));assert(folio_format_field_keep());
    capture_book(&ctx,out,"R01-unapplied-unchanged");assert(!memcmp(before,fb,sizeof(fb)));
    app_folio_format.on_enter(&ctx);draw(&app_folio_format,&ctx,out,"R04-real-unapplied");
    fail_alloc_after=0;assert(!folio_format_apply(&ctx));fail_alloc_after=-1;
    capture_book(&ctx,out,"R01-prepare-oom-unchanged");assert(!memcmp(before,fb,sizeof(fb))&&s_active_format.px==44);
    assert(folio_format_apply(&ctx));assert(s_active_format.px==48&&s_active_format.weight==FORMAT_BOLD&&s_active_format.background==FORMAT_PAPER_GRAY&&s_active_format.margin==FORMAT_MARGIN_WIDE);
    assert(book_layout_page_start_offset(s_page)<=anchor);if(s_page+1<book_layout_page_count())assert(book_layout_page_start_offset(s_page+1)>anchor);
    capture_book(&ctx,out,"R01-applied-real");assert(memcmp(before,fb,sizeof(fb))&&fb[0]==221);memcpy(before,fb,sizeof(fb));
    // 真正的字体传输 suspend 会关闭外部角色；取消返回必须按原身份重开，而非依赖旧ready状态。
    // Real font-transfer suspension closes external roles; cancel must reopen the original identity instead of trusting stale readiness.
    assert(folio_format_begin_book(&ctx));assert(folio_format_field_begin(FORMAT_FIELD_FONT));assert(folio_format_enter_font_child(false));assert(folio_format_enter_font_child(true));
    assert(ttf_font_suspend_sd(true)==ESP_OK);assert(ttf_font_suspend_sd(false)==ESP_OK);
    assert(folio_format_transfer_stopped());assert(folio_format_leave_font_child(true));assert(folio_format_field_cancel());assert(folio_format_discard(&ctx));
    capture_book(&ctx,out,"R01-after-real-transfer-cancel");assert(!memcmp(before,fb,sizeof(fb)));
    // DEFAULTS真实保存不会交换正文face/页表，也不修改当前书。
    // Persisting real DEFAULTS never swaps the reader face/pages or mutates the current book.
    assert(folio_format_begin_defaults(&ctx));keep_size(12);assert(folio_format_apply(&ctx));capture_book(&ctx,out,"R01-defaults-did-not-change-book");assert(!memcmp(before,fb,sizeof(fb)));
    // 已保存B优先：无关旧全局A缺失不阻断，有效B本身缺失才拒绝。
    // Saved B wins over missing unrelated global A; losing the effective B itself must fail closed.
    snprintf(default_font,sizeof(default_font),"/missing/unrelated-old-A.ttf");assert(open_book(&ctx,book_a));assert(s_active_format.px==48&&!strcmp(s_active_format.font.path,full_font));
    folio_format_values_t legacy=folio_format_builtin_defaults(),resolved;snprintf(legacy.font.path,sizeof(legacy.font.path),"%s",default_font);uint64_t revision;struct stat st;assert(!stat(book_b,&st));
    assert(folio_format_resolve_book(book_b,st.st_size,st.st_mtime,false,BOOK_PROGRESS_NATIVE,&legacy,&resolved,&revision));assert(resolved.px==56&&!strcmp(resolved.font.path,full_font));
    // M1 applied+unapplied迁移；M2 discard后旧记录虽保留也不复活。
    // Migrate M1 active+draft; retained old files never resurrect after M2 discard.
    assert(!stat(book_c,&st));folio_reader_draft_t old={0};folio_reader_draft_begin(&old,book_c,st.st_size,48);old.font_file_size=font_identity(full_font).file_size;old.book_mtime=st.st_mtime;snprintf(old.font_path,sizeof(old.font_path),"%s",full_font);assert(folio_reader_draft_adjust(&old,1));assert(folio_reader_draft_save(FORMAT_DIRECTORY,&old));
    assert(open_book(&ctx,book_c));assert(s_active_format.legacy_native_px==48);assert(folio_format_begin_book(&ctx));assert(folio_format_snapshot()->active.legacy_native_px==48&&folio_format_snapshot()->root.legacy_native_px==52);assert(folio_format_discard(&ctx));
    folio_format_reset_runtime();assert(open_book(&ctx,book_c));assert(s_active_format.legacy_native_px==48);assert(folio_format_begin_book(&ctx));assert(folio_format_snapshot()->root.legacy_native_px==48);assert(folio_format_discard(&ctx));
    // 完整未合并字段/字体子流程随ECO journal恢复，STOP通知幂等且不启动网络。
    // Restore unmerged fields/font lineage from ECO journal; STOP is idempotent and never starts networking.
    assert(folio_format_begin_book(&ctx));assert(folio_format_field_begin(FORMAT_FIELD_SIZE_WEIGHT));assert(folio_format_size_delta(4));
    folio_format_capture_power_origin(&app_folio_format);assert(folio_format_prepare_sleep(true));folio_format_reset_runtime();assert(folio_format_restore_boot()==&app_folio_format);assert(folio_format_snapshot()->candidate.px==40&&folio_format_snapshot()->candidate.legacy_native_px==0&&folio_format_snapshot()->root.legacy_native_px==48&&folio_format_snapshot()->active.legacy_native_px==48);assert(folio_format_field_cancel());
    assert(folio_format_field_begin(FORMAT_FIELD_FONT));assert(folio_format_enter_font_child(false));assert(folio_format_enter_font_child(true));unsigned depth=folio_format_snapshot()->return_depth;folio_format_capture_power_origin(&app_transfer);assert(!folio_format_prepare_sleep(true));assert(folio_format_transfer_stopped());assert(folio_format_transfer_stopped());assert(folio_format_snapshot()->return_depth==depth-1);assert(folio_format_prepare_sleep(true));folio_format_reset_runtime();assert(folio_format_restore_boot()==&app_folio_font_picker);assert(folio_format_snapshot()->route==FORMAT_ROUTE_FONT&&folio_format_snapshot()->font_transfer_stopped);
    folio_format_snapshot_t retained=*folio_format_snapshot();s_prep_task=(void*)1;folio_format_media_lost(&ctx);assert(workers_stopped==1&&!s_text&&!s_format_layout&&folio_format_snapshot()->resource_missing);assert(!memcmp(&retained.root,&folio_format_snapshot()->root,sizeof(retained.root)));ttf_font_media_lost();
    assert(folio_format_leave_font_child(true));assert(folio_format_field_cancel());assert(folio_format_discard(&ctx));folio_format_reset_runtime();
    // 第129本仅临时默认：不写owner/journal，取消/切书/睡眠不锁死，真编辑失败仍保留RAM。
    // Book 129 uses transient defaults without owner/journal growth; cancel/switch/sleep remain available and failed edits retain RAM.
    for(unsigned id=1;owned_books()<128;id++){char file[512];snprintf(file,sizeof(file),"%s/format_tx_%016llx.a",FORMAT_DIRECTORY,(unsigned long long)id);FILE*f=fopen(file,"wb");assert(f);assert(!fclose(f));}
    size_t files=directory_files();char overflow_book[288];snprintf(overflow_book,sizeof(overflow_book),"%s/overflow.txt",out);file_book(overflow_book);assert(!stat(overflow_book,&st));
    assert(folio_format_resolve_book(overflow_book,st.st_size,st.st_mtime,false,BOOK_PROGRESS_NATIVE,&legacy,&resolved,&revision));
    assert(revision==0&&resolved.px==56&&!resolved.legacy_native_px&&folio_format_read_baseline_pending(overflow_book));assert(directory_files()==files);
    assert(open_book(&ctx,overflow_book));assert(folio_format_begin_book(&ctx));assert(folio_format_snapshot()->transient_baseline&&!folio_format_snapshot()->edited);
    assert(folio_format_discard(&ctx));folio_format_capture_power_origin(&app_book);assert(folio_format_prepare_sleep(true));assert(directory_files()==files);
    assert(folio_format_prepare_book_change(book_a));assert(open_book(&ctx,book_a));folio_format_capture_power_origin(&app_book);assert(folio_format_prepare_sleep(true));
    // 模拟冷启动：明确v3临时默认标记使已有NVS进度也走M2默认，不再读缺失旧A或解释成native。
    // Cold restart: the explicit v3 transient marker selects M2 defaults even with saved progress, never old A/native sizing.
    free_book();folio_format_reset_runtime();assert(folio_format_resolve_book(overflow_book,st.st_size,st.st_mtime,true,BOOK_PROGRESS_TRANSIENT_DEFAULTS,&legacy,&resolved,&revision));
    assert(resolved.px==56&&!resolved.legacy_native_px&&!strcmp(resolved.font.path,full_font));assert(open_book(&ctx,overflow_book));
    assert(folio_format_begin_book(&ctx));keep_size(4);assert(!folio_format_apply(&ctx));assert(folio_format_snapshot()->root.px==60&&folio_format_snapshot()->active.px==56&&!folio_format_snapshot()->intent_attempted);
    folio_format_capture_power_origin(&app_folio_format);assert(!folio_format_prepare_sleep(true));assert(folio_format_discard(&ctx));assert(s_active_format.px==56);
    char absent[288];snprintf(absent,sizeof(absent),"%s/missing-new-prefs.txt",out);file_book(absent);assert(!stat(absent,&st));folio_format_values_t unchanged=resolved;
    assert(!folio_format_resolve_book(absent,st.st_size,st.st_mtime,true,BOOK_PROGRESS_FOLIO_EM,&legacy,&resolved,&revision));assert(!memcmp(&resolved,&unchanged,sizeof(resolved)));
    assert(directory_files()==files);
    printf("Device adapter + service + actual font/layout: candidate isolated, prepare OOM rollback, BOOK ACK, DEFAULTS isolation, M1 migration, journal restore and media-stop passed.\n");
    for(int role=0;role<TTF_ROLE_COUNT;role++){ttf_font_cache_stats_t stats;ttf_font_cache_stats(role,&stats);printf("font role %d: glyph=%zu resident=%zu stream=%zu mapped=%zu scratch=%zu\n",role,stats.glyph_bytes,stats.resident_bytes,stats.stream_bytes,stats.mapped_bytes,stats.scratch_bytes);ttf_font_scope_t font;if(ttf_font_begin(role,&font)==ESP_OK){ttf_font_unload();ttf_font_end(&font);}}
    book_layout_free();return 0;
}
