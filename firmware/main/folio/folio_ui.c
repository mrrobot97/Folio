/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：稳定设计稿的绘制原语，继续使用官方字体缓存和 EPD 绘图。
 * English: Stable-design primitives using the upstream glyph cache and EPD drawing.
 * 冻结：硬件采样与纯绘制分离；不逐秒请求刷新。
 * Frozen: Hardware sampling stays outside drawing; no second-by-second redraw requests.
 */
#include "folio_ui.h"
#include "ui_kit.h"
#include "ttf_font.h"
#include "read_pico_pmu.h"
#include "read_pico_sd.h"
#include <stdio.h>
#include <string.h>
#include "folio_clock.h"

void folio_ui_sample_status(folio_status_t *out) {
    memset(out, 0, sizeof(*out));
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    out->battery_percent = pmu && pmu->status_ok ? (int)(pmu->soc_permille / 10) : -1;
    if (out->battery_percent > 100) out->battery_percent = 100;
    read_pico_sd_info_t sd = {0};
    out->sd_ready = read_pico_sd_get_info(&sd) == ESP_OK && sd.mounted;
    int year, month, day, hour, minute;
    if (folio_clock_local(&year, &month, &day, &hour, &minute)) {
        static const char *week[] = {"一", "二", "三", "四", "五", "六", "日"};
        int weekday = (folio_ui_calendar_offset(year, month) + day - 1) % 7;
        snprintf(out->clock, sizeof(out->clock), "%d 月 %d 日 · 周%s", month, day, week[weekday]);
    } else {
        snprintf(out->clock, sizeof(out->clock), "日期未设置");
    }
}

// 设计字号是 CJK em；上游 API 是 ascent-descent 高度。/ Design size is CJK em; upstream uses ascent-descent height.
static int font_pixels(int em) {
    int advance = ttf_text_width_px(100, "中");
    int px = advance > 0 ? (em * 100 + advance / 2) / advance : em;
    return px > 120 ? 120 : px < 12 ? 12 : px;
}

void folio_ui_text(uint8_t *fb, int x, int y, int px, const char *text,
                   enum EpdFontFlags align, uint8_t ink, uint8_t paper) {
    ttf_draw_text_px(fb, x, y + (px * 88 + 50) / 100, font_pixels(px), text, align, ink >> 4, paper >> 4);
}

void folio_ui_text_fit(uint8_t *fb, int x, int y, int px, int width,
                       const char *text, uint8_t ink, uint8_t paper) {
    char clipped[256];
    if (!text || width <= 0) return;
    if (strlen(text) < sizeof(clipped) && ttf_text_width_px(font_pixels(px), text) <= width) {
        folio_ui_text(fb, x, y, px, text, EPD_DRAW_ALIGN_LEFT, ink, paper);
        return;
    }
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        size_t bytes = *p < 0x80 ? 1 : (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : 4;
        if (strlen((const char *)p) < bytes || used + bytes + 4 >= sizeof(clipped)) break;
        memcpy(clipped + used, p, bytes);
        memcpy(clipped + used + bytes, "…", 4);
        if (ttf_text_width_px(font_pixels(px), clipped) > width) break;
        used += bytes; p += bytes;
    }
    memcpy(clipped + used, "…", 4);
    folio_ui_text(fb, x, y, px, clipped, EPD_DRAW_ALIGN_LEFT, ink, paper);
}

static void line(uint8_t *fb, int x1, int y1, int x2, int y2) {
    epd_draw_line(x1, y1, x2, y2, FOLIO_INK, fb);
    epd_draw_line(x1 + 1, y1, x2 + 1, y2, FOLIO_INK, fb);
}
void folio_ui_chevron(uint8_t *fb, int x, int y, bool left) {
    int d = left ? -1 : 1;
    line(fb, x, y - 10, x + d * 10, y);
    line(fb, x + d * 10, y, x, y + 10);
}
void folio_ui_icon(uint8_t *fb, folio_icon_t icon, int x, int y, int s) {
    int a = s / 6, b = s - a, m = s / 2;
    switch (icon) {
    case FOLIO_ICON_HOME:
        line(fb, x+a,y+m,x+m,y+a); line(fb,x+m,y+a,x+b,y+m);
        line(fb,x+a+3,y+m,x+a+3,y+b); line(fb,x+a+3,y+b,x+b-3,y+b); line(fb,x+b-3,y+b,x+b-3,y+m); break;
    case FOLIO_ICON_LIBRARY:
        for(int i=0;i<3;i++) ui_draw_round_rect(fb,(EpdRect){x+a+i*s/4,y+a,s/6,s*2/3},0,FOLIO_INK);
        break;
    case FOLIO_ICON_SETTINGS:
        for(int i=0;i<3;i++){int xx=x+a+i*s/3;line(fb,xx,y+a,xx,y+b);epd_draw_circle(xx,y+a+5+(i%2)*s/3,s/10,FOLIO_INK,fb);} break;
    case FOLIO_ICON_SEARCH:
        epd_draw_circle(x+m-3,y+m-3,s/4,FOLIO_INK,fb); line(fb,x+m+s/6,y+m+s/6,x+b,y+b); break;
    case FOLIO_ICON_WIFI:
        line(fb,x+a,y+m,x+m,y+a);line(fb,x+m,y+a,x+b,y+m);
        line(fb,x+s/3,y+m+3,x+m,y+s/3);line(fb,x+m,y+s/3,x+s*2/3,y+m+3);
        epd_fill_circle(x+m,y+b,3,FOLIO_INK,fb);
        break;
    case FOLIO_ICON_BOOKMARK:
        line(fb,x+a,y+a,x+b,y+a);line(fb,x+a,y+a,x+a,y+b);line(fb,x+b,y+a,x+b,y+b);
        line(fb,x+a,y+b,x+m,y+b-8);line(fb,x+m,y+b-8,x+b,y+b);break;
    case FOLIO_ICON_REFRESH:
        epd_draw_circle(x+m,y+m,s/3,FOLIO_INK,fb);line(fb,x+b,y+a,x+b,y+m);line(fb,x+b,y+m,x+m,y+m);break;
    case FOLIO_ICON_BOOK:
        ui_draw_round_rect(fb,(EpdRect){x+a,y+a,s*2/3,s*2/3},2,FOLIO_INK);line(fb,x+m,y+a,x+m,y+b);break;
    case FOLIO_ICON_SLEEP:
        line(fb,x+b,y+a,x+m,y+a);line(fb,x+m,y+a,x+a,y+m);line(fb,x+a,y+m,x+m,y+b);
        line(fb,x+m,y+b,x+b,y+b);line(fb,x+b,y+b,x+m+4,y+m);line(fb,x+m+4,y+m,x+b,y+a);break;
    case FOLIO_ICON_DEVICE:
        ui_draw_round_rect(fb,(EpdRect){x+a,y,s*2/3,s},4,FOLIO_INK);line(fb,x+s/3,y+b,x+s*2/3,y+b);break;
    case FOLIO_ICON_MORE:
        for(int i=0;i<3;i++)epd_fill_circle(x+a+i*s/3,y+m,2,FOLIO_INK,fb);
        break;
    }
}
void folio_ui_status(uint8_t *fb, const folio_status_t *s) {
    char battery[16];
    folio_ui_text(fb,40,22,26,s->clock,EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    snprintf(battery,sizeof(battery),s->battery_percent<0?"--":"%d%%",s->battery_percent);
    folio_ui_text(fb,587,22,26,battery,EPD_DRAW_ALIGN_RIGHT,FOLIO_SECONDARY,0xFF);
    ui_draw_round_rect(fb,(EpdRect){602,29,35,19},3,FOLIO_INK);
    epd_fill_rect((EpdRect){640,35,3,7},FOLIO_INK,fb);
    if(s->battery_percent>0)epd_fill_rect((EpdRect){606,33,27*s->battery_percent/100,11},FOLIO_INK,fb);
}
void folio_ui_header(uint8_t *fb,const folio_status_t *s,const char *title,bool back){
    ui_clear_page(fb);folio_ui_status(fb,s);
    if(back)folio_ui_chevron(fb,62,126,true);
    folio_ui_text(fb,back?105:40,89,44,title,EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
}
void folio_ui_button(uint8_t *fb,EpdRect hit,const char *label,bool primary){
    EpdRect visible={hit.x,hit.y+12,hit.width,hit.height-24};
    uint8_t bg=primary?FOLIO_INK:FOLIO_SURFACE;
    ui_fill_round_rect(fb,visible,visible.height/2,bg);
    folio_ui_text(fb,hit.x+hit.width/2,hit.y+(hit.height-30)/2-2,30,label,EPD_DRAW_ALIGN_CENTER,primary?0xFF:FOLIO_INK,bg);
}
void folio_ui_row(uint8_t *fb,int y,const char *label,const char *value,uint8_t paper){
    int value_width=value?ttf_text_width_px(font_pixels(26),value):0;
    folio_ui_text_fit(fb,40,y+28,32,550-value_width-20,label,FOLIO_INK,paper);
    if(value)folio_ui_text(fb,590,y+32,26,value,EPD_DRAW_ALIGN_RIGHT,FOLIO_SECONDARY,paper);
    folio_ui_chevron(fb,623,y+50,false);ui_hairline(fb,y+110,40,604,FOLIO_RULE);
}
void folio_ui_tabs(uint8_t *fb,int selected){
    static const char *names[]={"主页","书架","设置"};
    static const folio_icon_t icons[]={FOLIO_ICON_HOME,FOLIO_ICON_LIBRARY,FOLIO_ICON_SETTINGS};
    ui_hairline(fb,1104,40,604,FOLIO_RULE);
    for(int i=0;i<3;i++){int x=40+208*i;uint8_t bg=i==selected?FOLIO_SURFACE:0xFF;
        if(i==selected)ui_fill_round_rect(fb,(EpdRect){x,1120,188,72},36,bg);
        folio_ui_icon(fb,icons[i],x+28,1138,32);
        folio_ui_text(fb,x+77,1137,30,names[i],EPD_DRAW_ALIGN_LEFT,FOLIO_INK,bg);
    }
}
int folio_ui_tab_hit(int x,int y){
    for(int i=0;i<3;i++)if(ui_rect_hit((EpdRect){40+208*i,1108,188,100},x,y))return i;
    return -1;
}
void folio_ui_duration(uint64_t ms,char *out,size_t cap){
    uint64_t min=ms/60000;
    if(ms>0&&min==0)snprintf(out,cap,"<1 分钟");
    else if(min<60)snprintf(out,cap,"%llu 分钟",(unsigned long long)min);
    else snprintf(out,cap,"%llu小时%llu分",(unsigned long long)(min/60),(unsigned long long)(min%60));
}
int folio_ui_month_days(int year,int month){
    static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(year<1||year>9999||month<1||month>12)return 0;
    return days[month-1]+(month==2&&(year%4==0&&(year%100!=0||year%400==0)));
}
bool folio_ui_date_valid(int day){int n=folio_ui_month_days(day/10000,day/100%100);return n&&day%100>=1&&day%100<=n;}
int folio_ui_previous_day(int day){
    if(!folio_ui_date_valid(day))return 0;
    int y=day/10000,m=day/100%100,d=day%100;
    if(--d==0){if(--m==0){m=12;y--;}d=folio_ui_month_days(y,m);}return y*10000+m*100+d;
}
int folio_ui_calendar_offset(int year,int month){
    static const int offsets[]={0,3,2,5,0,3,5,1,4,6,2,4};
    if(!folio_ui_month_days(year,month))return 0;
    if(month<3)year--;
    return (year+year/4-year/100+year/400+offsets[month-1]+7)%7;
}
EpdRect folio_ui_day_rect(int year,int month,int day){
    if(day<1||day>folio_ui_month_days(year,month))return (EpdRect){0};
    int i=folio_ui_calendar_offset(year,month)+day-1;
    return (EpdRect){6+(i%7)*96,402+(i/7)*96,96,96};
}
size_t folio_ui_selection_regions(int year,int month,int old_day,int new_day,EpdRect out[3]){
    if(old_day==new_day||new_day<1||new_day>folio_ui_month_days(year,month))return 0;
    size_t n=0;EpdRect old=folio_ui_day_rect(year,month,old_day);
    if(old.width)out[n++]=old;
    out[n++]=folio_ui_day_rect(year,month,new_day);out[n++]=(EpdRect){32,998,620,128};return n;
}
