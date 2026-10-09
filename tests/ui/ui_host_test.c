/* SPDX-License-Identifier: Apache-2.0
 * 中文：生产页面回调的主机几何/导航/差分验证；不模拟面板耗时。
 * English: Host checks of production drawing/navigation/diffs, never panel timing.
 */
#include "app.h"
#include "ui_kit.h"
#include "ui_gesture.h"
#include "ttf_font.h"
#include "folio_ui.h"
#include "folio_navigation.h"
#include "reading_service.h"
#include "reader_runtime.h"
#include "book_store.h"
#include "settings.h"
#include "read_pico_pmu.h"
#include "read_pico_sd.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define W 684
#define H 1216
static stbtt_fontinfo font;
static uint8_t fb[W*H],before[W*H];
static folio_reading_summary_t summary;
static int32_t snapshot_day, last_book_day;
static int64_t last_snapshot_ms;
static int nav,io_reads,format_calls,recovery_calls,retry_calls;
static bool flash_ready,setting_failure,pickup;
static app_sleep_mode_t sleep_mode=APP_SLEEP_DEEP;
static EpdRect updates[8];
static size_t update_count;
int E0470_WAVEFORM;
const app_desc_t app_transfer={0},app_font_pick={0},app_sleep={0},app_sd={0},app_home={0},app_folio_wallpaper={0},app_folio_datetime={0};
void epd_draw_pixel(int x,int y,uint8_t c,uint8_t*f){if(x>=0&&x<W&&y>=0&&y<H)f[y*W+x]=c;}
void epd_fill_rect(EpdRect r,uint8_t c,uint8_t*f){for(int y=r.y;y<r.y+r.height;y++)for(int x=r.x;x<r.x+r.width;x++)epd_draw_pixel(x,y,c,f);}
void epd_draw_line(int x,int y,int xx,int yy,uint8_t c,uint8_t*f){int dx=abs(xx-x),sx=x<xx?1:-1,dy=-abs(yy-y),sy=y<yy?1:-1,err=dx+dy;for(;;){epd_draw_pixel(x,y,c,f);if(x==xx&&y==yy)break;int e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}}
void epd_draw_circle(int x,int y,int r,uint8_t c,uint8_t*f){for(int a=0;a<720;a++){double theta=a*3.141592653589793/360;epd_draw_pixel(x+(int)round(r*cos(theta)),y+(int)round(r*sin(theta)),c,f);}}
void epd_fill_circle(int x,int y,int r,uint8_t c,uint8_t*f){for(int yy=-r;yy<=r;yy++)for(int xx=-r;xx<=r;xx++)if(xx*xx+yy*yy<=r*r)epd_draw_pixel(x+xx,y+yy,c,f);}
static bool inside(EpdRect r,int radius,int x,int y){int xx=x<r.x+radius?r.x+radius:x>=r.x+r.width-radius?r.x+r.width-radius-1:x;int yy=y<r.y+radius?r.y+radius:y>=r.y+r.height-radius?r.y+r.height-radius-1:y;return (x-xx)*(x-xx)+(y-yy)*(y-yy)<=radius*radius;}
void ui_fill_round_rect(uint8_t*f,EpdRect r,int radius,uint8_t c){for(int y=r.y;y<r.y+r.height;y++)for(int x=r.x;x<r.x+r.width;x++)if(inside(r,radius,x,y))epd_draw_pixel(x,y,c,f);}
void ui_draw_round_rect(uint8_t*f,EpdRect r,int radius,uint8_t c){EpdRect inner={r.x+1,r.y+1,r.width-2,r.height-2};for(int y=r.y;y<r.y+r.height;y++)for(int x=r.x;x<r.x+r.width;x++)if(inside(r,radius,x,y)&&(x<inner.x||x>=inner.x+inner.width||y<inner.y||y>=inner.y+inner.height||!inside(inner,radius?radius-1:0,x,y)))epd_draw_pixel(x,y,c,f);}
void ui_clear_page(uint8_t*f){memset(f,255,W*H);}
void ui_clear_rect_fast(uint8_t*f,EpdRect r){epd_fill_rect(r,255,f);}
EpdRect ui_rotate_rect_to_fb(EpdRect r){return r;}
EpdRect ui_rect_union(EpdRect a,EpdRect b){int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y,right=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width,bottom=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;return(EpdRect){x,y,right-x,bottom-y};}
bool ui_rect_hit(EpdRect r,uint16_t x,uint16_t y){return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
void ui_hairline(uint8_t*f,int y,int x,int w,uint8_t c){epd_fill_rect((EpdRect){x,y,w,1},c,f);}
static int utf8(const char**p){const unsigned char*s=(const unsigned char*)*p;int c=*s++,n=0;if(c>=0xF0){c&=7;n=3;}else if(c>=0xE0){c&=15;n=2;}else if(c>=0xC0){c&=31;n=1;}while(n--&&*s)c=(c<<6)|(*s++&63);*p=(const char*)s;return c;}
int ttf_ascender_px(int px){int a;stbtt_GetFontVMetrics(&font,&a,NULL,NULL);return (int)round(a*stbtt_ScaleForPixelHeight(&font,px));}
int ttf_text_width_px(int px,const char*s){float scale=stbtt_ScaleForPixelHeight(&font,px),width=0;while(*s){int c=utf8(&s),a;stbtt_GetCodepointHMetrics(&font,c,&a,NULL);width+=a*scale;}return (int)ceil(width);}
void ttf_draw_text_px(uint8_t*f,int x,int y,int px,const char*s,enum EpdFontFlags align,uint8_t ink,uint8_t paper){int width=ttf_text_width_px(px,s);if(align==EPD_DRAW_ALIGN_CENTER)x-=width/2;if(align==EPD_DRAW_ALIGN_RIGHT)x-=width;float scale=stbtt_ScaleForPixelHeight(&font,px),pos=(float)x;while(*s){int c=utf8(&s),w,h,xx,yy,a;unsigned char*glyph=stbtt_GetCodepointBitmap(&font,0,scale,c,&w,&h,&xx,&yy);stbtt_GetCodepointHMetrics(&font,c,&a,NULL);for(int j=0;j<h;j++)for(int i=0;i<w;i++){unsigned alpha=glyph[j*w+i];if(alpha){unsigned c8=(ink*17*alpha+paper*17*(255-alpha))/255;epd_draw_pixel((int)pos+xx+i,y+yy+j,(uint8_t)c8,f);}}stbtt_FreeBitmap(glyph,NULL);pos+=a*scale;}}
bool folio_clock_local(int*y,int*m,int*d,int*h,int*mi){if(!summary.today_day)return false;*y=summary.today_day/10000;*m=summary.today_day/100%100;*d=summary.today_day%100;*h=9;*mi=41;return true;}
const pmu_snapshot_t*read_pico_pmu_get(void){static pmu_snapshot_t p={true,760};io_reads++;return &p;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t*s){s->mounted=true;io_reads++;return ESP_OK;}
bool book_store_flash_ready(void){return flash_ready;}
uint64_t book_store_free_bytes(const book_store_root_t*r){(void)r;return 4*1024*1024;}
esp_err_t book_store_initialize_flash(bool confirmed){assert(confirmed);assert(!flash_ready);format_calls++;flash_ready=true;return ESP_OK;}
bool folio_reader_storage_reinitialized(void){recovery_calls++;return true;}
bool folio_reader_retry_storage(void){retry_calls++;return true;}
esp_err_t book_store_roots(book_store_root_t*r,int*n){(void)r;*n=0;io_reads++;return ESP_FAIL;}
folio_reading_summary_t folio_reading_summary(void){return summary;}
folio_reading_summary_t folio_reading_summary_at(int64_t now){last_snapshot_ms=now;folio_reading_summary_t value=summary;if(snapshot_day){value.today_day=snapshot_day;value.today_ms=folio_reading_day_ms(snapshot_day);}return value;}
void folio_reading_date_bounds(int32_t*first,int32_t*last){*first=summary.book_count?20261001:0;*last=summary.book_count?20261009:0;}
uint64_t folio_reading_day_ms(int32_t day){static const int minutes[]={20,0,18,24,10,0,35,16,12};return day>=20261001&&day<=20261009?(uint64_t)minutes[day-20261001]*60000:0;}
bool folio_reading_book(size_t i,folio_reading_book_t*out){if(i>=summary.book_count)return false;memset(out,0,sizeof(*out));snprintf(out->title,sizeof(out->title),"%s",i%2?"夜航手记":"山间来信");snprintf(out->path,sizeof(out->path),"/sdcard/books/%zu.txt",i);out->total_ms=summary.total_ms/summary.book_count+(i==0?summary.total_ms%summary.book_count:0);return true;}
uint64_t folio_reading_book_day_ms(size_t i,int32_t day){last_book_day=day;uint64_t total=folio_reading_day_ms(day);return i<2?(i?total-total*7/12:total*7/12):0;}
bool app_book_recent(char*t,size_t tc,char*p,size_t pc,unsigned*percent){io_reads++;snprintf(t,tc,"山间来信");snprintf(p,pc,"/sdcard/books/test.txt");*percent=32;return summary.book_count>0;}
void app_book_show_library(app_ctx_t*ctx){(void)ctx;nav=1;}
void app_book_show_search(app_ctx_t*ctx){(void)ctx;nav=4;}
void app_book_resume_last(app_ctx_t*ctx){(void)ctx;nav=2;}
void app_book_open_path(app_ctx_t*ctx,const char*p){(void)ctx;(void)p;nav=3;}
static uint8_t setting_px=48;static bool shake;
esp_err_t app_settings_storage_error(void){return setting_failure?ESP_FAIL:ESP_OK;}
const char*app_settings_wallpaper_path(void){return "";}
uint8_t app_settings_book_px(void){return setting_px;}
void app_settings_set_book_px(uint8_t p){setting_px=p;}
bool app_settings_book_shake(void){return shake;}
void app_settings_set_book_shake(bool b){shake=b;}
bool app_settings_pickup_wake(void){return pickup;}
void app_settings_set_pickup_wake(bool p){if(!setting_failure)pickup=p;}
void app_settings_set_sleep_mode(app_sleep_mode_t mode){if(!setting_failure)sleep_mode=mode;}
app_sleep_mode_t app_settings_sleep_mode(void){return sleep_mode;}
enum EpdDrawError update_display_area_with(EpdiyHighlevelState*h,const void*w,enum EpdDrawMode m,EpdRect r){(void)h;(void)w;assert(m==MODE_GL16);assert(update_count<8);updates[update_count++]=r;return EPD_DRAW_SUCCESS;}
void guard_draw_result(EpdiyHighlevelState*h,enum EpdDrawError e){(void)h;assert(e==EPD_DRAW_SUCCESS);}
static app_redraw_t tap(const app_desc_t*a,app_ctx_t*c,int x,int y){ui_gesture_event_t e={.type=UI_GESTURE_TAP,.x0=x,.y0=y,.x=x,.y=y};return a->on_gesture(c,&e);}
static void draw(const app_desc_t*a,app_ctx_t*c,const char*dir,const char*name){int reads=io_reads;a->render(c,fb);assert(io_reads==reads);char path[512];snprintf(path,sizeof(path),"%s/%s.pgm",dir,name);FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P5\n%d %d\n255\n",W,H);assert(fwrite(fb,1,sizeof(fb),f)==sizeof(fb));fclose(f);}
int main(int argc,char**argv){
    assert(argc==3);FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long len=ftell(f);rewind(f);unsigned char*font_data=malloc((size_t)len);assert(font_data&&fread(font_data,1,(size_t)len,f)==(size_t)len);fclose(f);assert(stbtt_InitFont(&font,font_data,0));
    assert(folio_ui_calendar_offset(2026,10)==3);assert(folio_ui_month_days(2024,2)==29);assert(folio_ui_month_days(2100,2)==28);assert(folio_ui_previous_day(20260101)==20251231);
    for(int y=2024;y<2036;y++)for(int m=1;m<=12;m++)for(int d=1;d<=folio_ui_month_days(y,m);d++){EpdRect r=folio_ui_day_rect(y,m,d);assert(r.x>=0&&r.y>=402&&r.x+r.width<=W&&r.y+r.height<=978);}
    char duration[48];folio_ui_duration(1,duration,sizeof(duration));assert(!strcmp(duration,"<1 分钟"));folio_ui_duration(0,duration,sizeof(duration));assert(!strcmp(duration,"0 分钟"));
    app_ctx_t ctx={.fb=fb};summary=(folio_reading_summary_t){.total_ms=30960000,.today_ms=720000,.today_day=20261009,.book_count=2,.storage_ok=true};
    app_folio_home.on_enter(&ctx);draw(&app_folio_home,&ctx,argv[2],"home-resume");tap(&app_folio_home,&ctx,300,1140);assert(nav==1);tap(&app_folio_home,&ctx,500,470);assert(nav==2);tap(&app_folio_home,&ctx,100,470);assert(ctx.request_app==&app_folio_stats);
    app_folio_stats.on_enter(&ctx);draw(&app_folio_stats,&ctx,argv[2],"stats-overview");tap(&app_folio_stats,&ctx,200,746);draw(&app_folio_stats,&ctx,argv[2],"stats-calendar");memcpy(before,fb,sizeof(fb));
    assert(tap(&app_folio_stats,&ctx,342,544)==APP_REDRAW_AREA);assert(app_folio_stats.present(&ctx,APP_REDRAW_AREA));assert(update_count==1);
    size_t area=0,changed=0;for(size_t i=0;i<update_count;i++)area+=(size_t)updates[i].width*updates[i].height;
    for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(before[y*W+x]!=fb[y*W+x]){changed++;bool covered=false;for(size_t i=0;i<update_count;i++)if(ui_rect_hit(updates[i],x,y))covered=true;assert(covered);}
    assert(area==389360);printf("Calendar selection: 3 dirty regions -> 1 GL16 submission; union %zu pixels / %d = %.2f%%; %zu changed pixels, all inside union (not physical scan evidence).\n",area,W*H,100.0*area/(W*H),changed);
    memcpy(before,fb,sizeof(fb));draw(&app_folio_stats,&ctx,argv[2],"stats-calendar-selected");assert(!memcmp(before,fb,sizeof(fb)));assert(tap(&app_folio_stats,&ctx,534,544)==APP_REDRAW_NONE);tap(&app_folio_stats,&ctx,300,1044);draw(&app_folio_stats,&ctx,argv[2],"stats-day");tap(&app_folio_stats,&ctx,55,125);draw(&app_folio_stats,&ctx,argv[2],"stats-calendar-return");assert(tap(&app_folio_stats,&ctx,342,544)==APP_REDRAW_NONE);
    tap(&app_folio_stats,&ctx,342,224);draw(&app_folio_stats,&ctx,argv[2],"stats-month-picker");tap(&app_folio_stats,&ctx,55,125);
    tap(&app_folio_stats,&ctx,55,125);tap(&app_folio_stats,&ctx,450,260);draw(&app_folio_stats,&ctx,argv[2],"stats-books");tap(&app_folio_stats,&ctx,100,450);draw(&app_folio_stats,&ctx,argv[2],"stats-book-detail");
    app_folio_settings.on_enter(&ctx);draw(&app_folio_settings,&ctx,argv[2],"settings");tap(&app_folio_settings,&ctx,200,445);draw(&app_folio_settings,&ctx,argv[2],"settings-reading");tap(&app_folio_settings,&ctx,550,460);assert(setting_px==52);
    ctx.now_ms=23000;app_folio_home.on_enter(&ctx);assert(last_snapshot_ms==23000);
    snapshot_day=20261010;ctx.now_ms=24000;assert(app_folio_home.on_tick(&ctx)==APP_REDRAW_PAGE);
    ctx.now_ms=24001;assert(app_folio_home.on_tick(&ctx)==APP_REDRAW_NONE);
    ctx.now_ms=25000;assert(app_folio_home.on_tick(&ctx)==APP_REDRAW_NONE);
    snapshot_day=0;folio_stats_open(&ctx);app_folio_stats.on_enter(&ctx);assert(last_snapshot_ms==25000);
    snapshot_day=20261010;ctx.now_ms=26000;assert(app_folio_stats.on_tick(&ctx)==APP_REDRAW_PAGE);
    ctx.now_ms=27000;assert(app_folio_stats.on_tick(&ctx)==APP_REDRAW_NONE);
    tap(&app_folio_stats,&ctx,100,260);assert(last_book_day==20261010);
    snapshot_day=20261011;ctx.now_ms=86427000;assert(app_folio_stats.on_tick(&ctx)==APP_REDRAW_PAGE);
    assert(last_book_day==20261010);assert(summary.total_ms==30960000);snapshot_day=0;
    summary=(folio_reading_summary_t){.total_ms=5000,.unknown_ms=5000,.dirty=true};folio_stats_open(&ctx);app_folio_stats.on_enter(&ctx);draw(&app_folio_stats,&ctx,argv[2],"stats-unknown");tap(&app_folio_stats,&ctx,100,260);draw(&app_folio_stats,&ctx,argv[2],"stats-unknown-no-today");tap(&app_folio_stats,&ctx,100,744);draw(&app_folio_stats,&ctx,argv[2],"stats-calendar-unknown");
    app_folio_home.on_enter(&ctx);draw(&app_folio_home,&ctx,argv[2],"home-empty");
    summary.book_count=2;folio_stats_open(&ctx);app_folio_stats.on_enter(&ctx);tap(&app_folio_stats,&ctx,100,880);draw(&app_folio_stats,&ctx,argv[2],"stats-saved-history-no-clock");tap(&app_folio_stats,&ctx,100,380);draw(&app_folio_stats,&ctx,argv[2],"stats-saved-day-no-clock");
    ctx.sensor_ready=true;app_folio_sleep.on_enter(&ctx);draw(&app_folio_sleep,&ctx,argv[2],"sleep-eco");
    tap(&app_folio_sleep,&ctx,200,550);assert(sleep_mode==APP_SLEEP_LIGHT);draw(&app_folio_sleep,&ctx,argv[2],"sleep-fast");
    tap(&app_folio_sleep,&ctx,200,748);draw(&app_folio_sleep,&ctx,argv[2],"sleep-pickup");tap(&app_folio_sleep,&ctx,200,400);assert(pickup);
    tap(&app_folio_sleep,&ctx,200,1140);tap(&app_folio_sleep,&ctx,200,980);assert(ctx.request_sleep);assert(!ctx.request_poweroff);
    tap(&app_folio_sleep,&ctx,500,980);draw(&app_folio_sleep,&ctx,argv[2],"sleep-shutdown-confirm");assert(!ctx.request_poweroff);tap(&app_folio_sleep,&ctx,200,980);assert(ctx.request_poweroff);
    setting_failure=true;app_folio_sleep.on_enter(&ctx);tap(&app_folio_sleep,&ctx,200,420);assert(sleep_mode==APP_SLEEP_LIGHT);draw(&app_folio_sleep,&ctx,argv[2],"sleep-save-failure");setting_failure=false;
    ctx.sensor_ready=false;pickup=false;app_folio_sleep.on_enter(&ctx);assert(tap(&app_folio_sleep,&ctx,200,748)==APP_REDRAW_NONE);assert(!pickup);
    app_folio_storage.on_enter(&ctx);draw(&app_folio_storage,&ctx,argv[2],"storage-unavailable");assert(!format_calls);
    tap(&app_folio_storage,&ctx,200,620);assert(!format_calls);tap(&app_folio_storage,&ctx,200,756);draw(&app_folio_storage,&ctx,argv[2],"storage-warning");assert(!format_calls);
    tap(&app_folio_storage,&ctx,200,1140);assert(!format_calls);tap(&app_folio_storage,&ctx,200,756);tap(&app_folio_storage,&ctx,200,982);draw(&app_folio_storage,&ctx,argv[2],"storage-final-confirm");assert(!format_calls);
    tap(&app_folio_storage,&ctx,200,982);assert(format_calls==1&&recovery_calls==1);draw(&app_folio_storage,&ctx,argv[2],"storage-ready");
    tap(&app_folio_storage,&ctx,200,620);assert(retry_calls==1&&recovery_calls==1&&format_calls==1);
    app_folio_storage.on_enter(&ctx);tap(&app_folio_storage,&ctx,200,756);tap(&app_folio_storage,&ctx,200,982);assert(format_calls==1);
    flash_ready=false;app_folio_storage.on_enter(&ctx);tap(&app_folio_storage,&ctx,200,756);tap(&app_folio_storage,&ctx,200,982);flash_ready=true;tap(&app_folio_storage,&ctx,200,982);assert(format_calls==1);
    free(font_data);puts("PASS: production rendering purity, real routes, leap/year calendar bounds and gray-region containment.");return 0;
}
