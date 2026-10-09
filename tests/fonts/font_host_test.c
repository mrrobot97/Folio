/* SPDX-License-Identifier: Apache-2.0
 * 生产字体引擎的主机回归，使用真实 gvar/光栅化；不模拟面板时序。
 * Host regression using production gvar/rasterization; never simulates panel timing.
 */
#include "ttf_font.h"
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define W 684
#define H 720
static size_t used,peak;
static size_t fail_after=SIZE_MAX;
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) {fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);abort();} } while(0)
void host_log(const char*tag,const char*format,...){(void)tag;(void)format;}
void* heap_caps_malloc(size_t n,unsigned caps){(void)caps;if(fail_after==0)return NULL;if(fail_after!=SIZE_MAX)fail_after--;size_t*p=malloc(n+sizeof(size_t));if(!p)return NULL;*p=n;used+=n;if(used>peak)peak=used;return p+1;}
void* heap_caps_calloc(size_t n,size_t size,unsigned caps){void*p=heap_caps_malloc(n*size,caps);if(p)memset(p,0,n*size);return p;}
void heap_caps_free(void*p){if(p){size_t*q=(size_t*)p-1;assert(used>=*q);used-=*q;free(q);}}
size_t heap_caps_get_largest_free_block(unsigned caps){(void)caps;return 8u*1024u*1024u-used;}
int64_t esp_timer_get_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
const char* app_settings_font_path(void){return "builtin";}
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t*fb){if(x>=0&&x<W&&y>=0&&y<H)fb[y*W+x]=(uint8_t)((c>>4)*17);}
static uint64_t ink(const uint8_t*fb){uint64_t n=0;for(int i=0;i<W*H;i++)n+=255-fb[i];return n;}
static void render(ttf_font_role_t role,int weight,uint8_t*fb){ttf_font_scope_t scope;CHECK(ttf_font_begin(role,&scope)==ESP_OK);ttf_set_weight(weight);memset(fb,255,W*H);int px=ttf_font_pixel_height_for_em(44);CHECK(px>44&&px<=120);ttf_draw_text_px(fb,32,ttf_ascender_px(px)+24,px,"山间来信 Hello 012",EPD_DRAW_ALIGN_LEFT,0,15);ttf_font_end(&scope);}
static void save(const char*out,const char*name,const uint8_t*fb){char path[512];snprintf(path,sizeof(path),"%s/%s.pgm",out,name);FILE*f=fopen(path,"wb");CHECK(f);fprintf(f,"P5\n%d %d\n255\n",W,H);CHECK(fwrite(fb,1,W*H,f)==W*H);fclose(f);}
static void copy_file(const char*src,const char*dst){FILE*a=fopen(src,"rb"),*b=fopen(dst,"wb");CHECK(a&&b);char buf[8192];size_t n;while((n=fread(buf,1,sizeof(buf),a))>0)CHECK(fwrite(buf,1,n,b)==n);fclose(a);fclose(b);}
static atomic_bool worker_started,worker_done;
static void* worker(void*arg){(void)arg;atomic_store(&worker_started,true);ttf_font_scope_t scope;assert(ttf_font_begin(TTF_ROLE_READER,&scope)==ESP_OK);assert(ttf_get_weight()==400);ttf_set_weight(500);ttf_font_end(&scope);atomic_store(&worker_done,true);return NULL;}
int main(int argc,char**argv){
    CHECK(argc==3);const char*font=argv[1];const char*out=argv[2];uint8_t*a=malloc(W*H),*b=malloc(W*H),*c=malloc(W*H);CHECK(a&&b&&c);
    CHECK(ttf_font_init()==ESP_OK);ttf_font_scope_t ui_scope;CHECK(ttf_font_begin(TTF_ROLE_UI,&ui_scope)==ESP_OK);CHECK(ttf_get_weight()==400);int low,high;CHECK(ttf_font_weight_range(&low,&high)&&low<=400&&high>=800);ttf_font_end(&ui_scope);CHECK(ttf_font_prepare(TTF_ROLE_READER,font)==ESP_OK);
    ttf_font_scope_t scope;CHECK(ttf_font_begin(TTF_ROLE_READER,&scope)==ESP_OK);int px=ttf_font_pixel_height_for_em(44);CHECK(ttf_font_pixel_height_for_em(80)==116);int width=ttf_text_width_px(px,"山间来信 Hello 012");CHECK(ttf_font_check_text("山间来信 Hello 012",NULL));ttf_set_weight(400);ttf_font_end(&scope);
    render(TTF_ROLE_READER,400,a);render(TTF_ROLE_UI,600,c);ttf_font_cache_stats_t reader_before,reader_after,ui_before,ui_after;
    ttf_font_cache_stats(TTF_ROLE_READER,&reader_before);ttf_font_cache_stats(TTF_ROLE_UI,&ui_before);
    CHECK(ttf_font_prepare(TTF_ROLE_PREVIEW,font)==ESP_OK);render(TTF_ROLE_PREVIEW,800,b);CHECK(ink(b)>ink(a));save(out,"reader-weight400",a);save(out,"preview-weight800",b);
    CHECK(ttf_font_begin(TTF_ROLE_PREVIEW,&scope)==ESP_OK);CHECK(ttf_text_width_px(px,"山间来信 Hello 012")==width);CHECK(ttf_get_weight()==800);ttf_font_end(&scope);
    render(TTF_ROLE_READER,400,b);CHECK(memcmp(a,b,W*H)==0);ttf_font_cache_stats(TTF_ROLE_READER,&reader_after);CHECK(reader_after.opens==reader_before.opens);CHECK(reader_after.misses==reader_before.misses);CHECK(reader_after.hits>reader_before.hits);
    render(TTF_ROLE_UI,600,b);CHECK(memcmp(c,b,W*H)==0);ttf_font_cache_stats(TTF_ROLE_UI,&ui_after);CHECK(ui_after.opens==ui_before.opens);CHECK(ui_after.misses==ui_before.misses);
    ttf_font_discard_preview();render(TTF_ROLE_READER,400,b);CHECK(memcmp(a,b,W*H)==0);
    CHECK(ttf_font_prepare(TTF_ROLE_PREVIEW,"/missing/font.ttf")==ESP_ERR_NOT_FOUND);render(TTF_ROLE_READER,400,b);CHECK(memcmp(a,b,W*H)==0);
    CHECK(ttf_font_prepare(TTF_ROLE_PREVIEW,font)==ESP_OK);CHECK(ttf_font_begin(TTF_ROLE_PREVIEW,&scope)==ESP_OK);ttf_set_weight(800);ttf_font_end(&scope);CHECK(ttf_font_apply_preview()==ESP_OK);CHECK(ttf_get_weight()==800);CHECK(ttf_font_apply_preview()==ESP_OK);CHECK(ttf_get_weight()==400);ttf_font_discard_preview();
    // 同任务 UI 嵌套和后台线程都不能污染正文。/ Nested UI and a concurrent worker cannot leak role state.
    CHECK(ttf_font_begin(TTF_ROLE_PREVIEW,&scope)==ESP_ERR_INVALID_STATE);CHECK(ttf_font_begin(TTF_ROLE_UI,&scope)==ESP_OK);ttf_set_weight(700);
    pthread_t thread;CHECK(pthread_create(&thread,NULL,worker,NULL)==0);while(!atomic_load(&worker_started)){}
    struct timespec pause={0,10000000};nanosleep(&pause,NULL);CHECK(!atomic_load(&worker_done));CHECK(ttf_get_weight()==700);ttf_font_end(&scope);pthread_join(thread,NULL);CHECK(atomic_load(&worker_done));CHECK(ttf_get_weight()==500);
    // 缺字必须可检测，子集不能冒充完整中文支持。/ Missing glyphs are detectable; the subset is not a full CJK face.
    CHECK(ttf_font_begin(TTF_ROLE_UI,&scope)==ESP_OK);CHECK(!ttf_font_has_codepoint(0x10ffff));uint32_t missing=0;CHECK(!ttf_font_check_text("\xf4\x8f\xbf\xbf",&missing)&&missing==0x10ffff);uint32_t subset_missing=0;for(uint32_t cp=0x4e00;cp<0x9fff;cp++)if(!ttf_font_has_codepoint(cp)){subset_missing=cp;break;}CHECK(subset_missing);ttf_font_end(&scope);CHECK(ttf_font_has_codepoint(subset_missing));
    // 压满每个角色的 LRU，验证总字形预算和淘汰。/ Fill each LRU to verify caps/eviction.
    CHECK(ttf_font_prepare(TTF_ROLE_PREVIEW,font)==ESP_OK);
    for(int role=0;role<TTF_ROLE_COUNT;role++){CHECK(ttf_font_begin((ttf_font_role_t)role,&scope)==ESP_OK);for(int size=20;size<=160;size+=4)for(int weight=300;weight<=800;weight+=100){ttf_set_weight(weight);ttf_draw_text_px(b,0,120,size,"ABCDEFGHIJKLMNOPQRSTUVWXYZ山间来信",EPD_DRAW_ALIGN_LEFT,0,15);}ttf_font_end(&scope);}
    size_t total=0;for(int role=0;role<TTF_ROLE_COUNT;role++){ttf_font_cache_stats_t stats;ttf_font_cache_stats((ttf_font_role_t)role,&stats);CHECK(stats.glyph_bytes<=stats.glyph_limit);CHECK(stats.evictions>0);total+=stats.glyph_bytes;printf("role=%d cache=%zu/%zu hit=%u miss=%u eviction=%u open=%u\n",role,stats.glyph_bytes,stats.glyph_limit,stats.hits,stats.misses,stats.evictions,stats.opens);printf("  resident=%zu stream=%zu mapped=%zu scratch=%zu\n",stats.resident_bytes,stats.stream_bytes,stats.mapped_bytes,stats.scratch_bytes);}CHECK(total<=1536u*1024u);
    // 资源身份、暂停和重新载入。/ Resource identity, suspension and explicit reload.
    ttf_font_identity_t first,second;CHECK(ttf_font_identity_read(font,&first)==ESP_OK);CHECK(ttf_font_identity_read(font,&second)==ESP_OK);CHECK(ttf_font_identity_equal(&first,&second));second.mtime++;CHECK(!ttf_font_identity_equal(&first,&second));
    CHECK(ttf_font_suspend_sd(true)==ESP_OK);CHECK(!ttf_font_ready()&&ttf_font_resource_failed());CHECK(!strcmp(ttf_font_path(),font));CHECK(ttf_font_prepare(TTF_ROLE_PREVIEW,font)==ESP_ERR_INVALID_STATE);CHECK(ttf_font_begin(TTF_ROLE_UI,&scope)==ESP_OK);ttf_font_end(&scope);CHECK(ttf_font_suspend_sd(false)==ESP_OK);CHECK(!ttf_font_ready());CHECK(ttf_font_prepare(TTF_ROLE_READER,font)==ESP_OK);
    char bad[512];snprintf(bad,sizeof(bad),"%s/broken.ttf",out);FILE*f=fopen(bad,"wb");CHECK(f);fputs("broken",f);fclose(f);CHECK(ttf_font_prepare(TTF_ROLE_PREVIEW,bad)==ESP_ERR_INVALID_RESPONSE);CHECK(ttf_font_ready());
    char copy[512];snprintf(copy,sizeof(copy),"%s/copy.ttf",out);copy_file(font,copy);CHECK(ttf_font_identity_read(copy,&first)==ESP_OK);int fd=open(copy,O_RDWR);CHECK(fd>=0);uint8_t byte;CHECK(pread(fd,&byte,1,16)==1);byte^=1;CHECK(pwrite(fd,&byte,1,16)==1);close(fd);CHECK(ttf_font_identity_read(copy,&second)==ESP_OK);CHECK(first.file_size==second.file_size&&first.header_crc!=second.header_crc);
    CHECK(ttf_font_prepare(TTF_ROLE_PREVIEW,copy)==ESP_OK);
    CHECK(truncate(copy,524)==0);CHECK(ttf_font_begin(TTF_ROLE_PREVIEW,&scope)==ESP_OK);ttf_font_cache_clear();ttf_draw_text_px(b,0,100,64,"新字",EPD_DRAW_ALIGN_LEFT,0,15);CHECK(ttf_font_resource_failed()&&!ttf_font_ready());ttf_font_end(&scope);CHECK(ttf_font_ready());
    CHECK(ttf_font_prepare(TTF_ROLE_READER,ttf_font_path())==ESP_OK);
    fail_after=0;CHECK(ttf_font_prepare(TTF_ROLE_PREVIEW,font)==ESP_ERR_NO_MEM);fail_after=SIZE_MAX;CHECK(ttf_font_ready());
    ttf_font_media_lost();CHECK(!ttf_font_ready()&&ttf_font_resource_failed());CHECK(ttf_font_begin(TTF_ROLE_UI,&scope)==ESP_OK);ttf_font_unload();ttf_font_end(&scope);ttf_font_discard_preview();ttf_font_unload();CHECK(used==0);
    printf("production font engine: %u checks; em44=%dpx, width=%dpx, peak PSRAM allocations=%zu bytes; no panel timing claim\n",checks,px,width,peak);free(a);free(b);free(c);return 0;
}
