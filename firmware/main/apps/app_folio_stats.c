/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：真实本地统计总览、按日/按书明细和月历；来自 v0.4.2 ST 系列。
 * English: Real local totals, daily/book detail and calendar from v0.4.2 ST designs.
 * 冻结：render 纯绘制；未知日期单列；选日只画两个格和明细，保留驱动清理策略。
 * Frozen: Pure render, separate undated time, selection paints two cells and detail with driver cleanup retained.
 */
#include "app.h"
#include "folio_ui.h"
#include "folio_navigation.h"
#include "reading_service.h"
#include "ui_kit.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include "display.h"
#include "e0470_epaper_waveform.h"
#include <stdio.h>
#include <string.h>

extern const app_desc_t app_folio_datetime;
typedef enum { OVERVIEW, CALENDAR, DAY, BOOKS, BOOK, RULES, HISTORY, MONTHS } stats_view_t;
static struct {
    folio_status_t status;
    folio_reading_summary_t summary;
    int64_t date_check_ms;
    folio_reading_book_t books[FOLIO_READING_MAX_BOOKS];
    uint64_t book_day[FOLIO_READING_MAX_BOOKS], month_ms[31], week_ms[7];
    int week_day[7], year, month, selected_day, page, selected_book, day_parent_page, book_parent_page, rules_parent_page, picker_year;
    int32_t first_record, last_record;
    stats_view_t view, day_parent, book_parent, rules_parent;
    EpdRect dirty[3]; size_t dirty_count;
    bool reset;
} s_stats;

void folio_stats_open(app_ctx_t *ctx){s_stats.reset=true;ctx->request_app=&app_folio_stats;}
static int upper_date(void){
    // 校时回拨后仍可从历史查看较晚记录，日历不虚构未来。/ After clock rollback, history retains later records without inventing future calendar days.
    if(s_stats.view==HISTORY&&s_stats.last_record>s_stats.summary.today_day)return s_stats.last_record;
    return s_stats.summary.today_day?s_stats.summary.today_day:s_stats.last_record;
}
static int selected_date(void){return s_stats.year*10000+s_stats.month*100+s_stats.selected_day;}
static void load_month(void){
    memset(s_stats.month_ms,0,sizeof(s_stats.month_ms));
    for(int d=1;d<=folio_ui_month_days(s_stats.year,s_stats.month);d++)
        s_stats.month_ms[d-1]=folio_reading_day_ms(s_stats.year*10000+s_stats.month*100+d);
}
static void load_day(void){
    for(size_t i=0;i<s_stats.summary.book_count;i++)s_stats.book_day[i]=folio_reading_book_day_ms(i,selected_date());
}
static void on_enter(app_ctx_t *ctx){
    folio_ui_sample_status(&s_stats.status);s_stats.summary=folio_reading_summary_at(ctx->now_ms);
    s_stats.date_check_ms=ctx->now_ms;
    folio_reading_date_bounds(&s_stats.first_record,&s_stats.last_record);
    if(s_stats.summary.book_count>FOLIO_READING_MAX_BOOKS)s_stats.summary.book_count=FOLIO_READING_MAX_BOOKS;
    for(size_t i=0;i<s_stats.summary.book_count;i++)folio_reading_book(i,&s_stats.books[i]);
    if(s_stats.reset||!s_stats.year){
        if(s_stats.reset){s_stats.view=OVERVIEW;s_stats.page=0;}
        s_stats.reset=false;
        int date=upper_date();
        s_stats.year=date/10000;s_stats.month=date/100%100;s_stats.selected_day=date%100;
    }
    int day=s_stats.summary.today_day;
    for(int i=6;i>=0;i--){s_stats.week_day[i]=day;s_stats.week_ms[i]=day?folio_reading_day_ms(day):0;day=folio_ui_previous_day(day);}
    load_month();load_day();s_stats.dirty_count=0;
}
static app_redraw_t on_tick(app_ctx_t *ctx){
    if(ctx->now_ms>=s_stats.date_check_ms&&ctx->now_ms-s_stats.date_check_ms<1000)return APP_REDRAW_NONE;
    s_stats.date_check_ms=ctx->now_ms;
    folio_reading_summary_t current=folio_reading_summary_at(ctx->now_ms);
    if(current.today_day==s_stats.summary.today_day)return APP_REDRAW_NONE;
    // 保留正在查看的历史日期和返回栈，只更新今日/周统计及日期可选范围。
    // Preserve the selected historical date and return stack; update today/week and selectable-date bounds.
    on_enter(ctx);
    return APP_REDRAW_PAGE;
}
static void duration(uint8_t *fb,int x,int y,int px,uint64_t ms,enum EpdFontFlags align,uint8_t paper){
    char text[48];folio_ui_duration(ms,text,sizeof(text));folio_ui_text(fb,x,y,px,text,align,FOLIO_INK,paper);
}
static void save_note(uint8_t *fb,int y){
    const char *note=NULL;
    if(!s_stats.summary.storage_ok)note="统计存储不可用 · 本次记录尚未保存";
    else if(s_stats.summary.dirty)note="阅读时长尚未保存";
    else if(s_stats.summary.capacity_full)note="统计容量已满 · 部分时长未分类";
    if(note)folio_ui_text_fit(fb,40,y,24,604,note,FOLIO_SECONDARY,0xFF);
}
static void render_overview(uint8_t *fb){
    folio_ui_header(fb,&s_stats.status,"本地阅读统计",true);
    ui_fill_round_rect(fb,(EpdRect){32,202,620,176},20,FOLIO_SURFACE);
    folio_ui_text(fb,56,224,26,s_stats.summary.today_day?"今日 ›":"今日",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,FOLIO_SURFACE);
    folio_ui_text(fb,360,224,26,"累计 ›",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,FOLIO_SURFACE);
    if(s_stats.summary.today_day)duration(fb,56,270,44,s_stats.summary.today_ms,EPD_DRAW_ALIGN_LEFT,FOLIO_SURFACE);
    else folio_ui_text(fb,56,270,44,"—",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,FOLIO_SURFACE);
    char total[48];folio_ui_duration(s_stats.summary.total_ms,total,sizeof(total));
    folio_ui_text_fit(fb,360,272,38,270,total,FOLIO_INK,FOLIO_SURFACE);
    if(s_stats.summary.today_day){
        folio_ui_text(fb,40,414,28,"最近 7 天",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        uint64_t max=1;for(int i=0;i<7;i++)if(s_stats.week_ms[i]>max)max=s_stats.week_ms[i];
        for(int i=0;i<7;i++){
            int h=(int)(s_stats.week_ms[i]*140/max);if(!h)h=2;
            ui_fill_round_rect(fb,(EpdRect){52+i*86,618-h,54,h},h>5?4:0,i==6?FOLIO_INK:0xBB);
            char d[8];snprintf(d,sizeof(d),"%d",s_stats.week_day[i]%100);
            folio_ui_text(fb,79+i*86,633,24,d,EPD_DRAW_ALIGN_CENTER,FOLIO_SECONDARY,0xFF);
        }
    }else{
        folio_ui_text(fb,40,420,28,"日期未设置。",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,468,26,"累计与单本继续记录，今日不猜测归档。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_button(fb,(EpdRect){40,566,604,96},"设置日期",false);
    }
    folio_ui_row(fb,700,"阅读热力图","按日期查看",0xFF);
    folio_ui_row(fb,824,"历史记录","本地保存",0xFF);
    save_note(fb,943);
    folio_ui_button(fb,(EpdRect){40,976,290,96},"单本书时长",false);
    folio_ui_button(fb,(EpdRect){346,976,298,96},"计时规则",true);
    folio_ui_button(fb,(EpdRect){40,1100,604,96},"返回",false);
}
static void draw_cell(uint8_t *fb,int day){
    EpdRect hit=folio_ui_day_rect(s_stats.year,s_stats.month,day);
    if(!hit.width)return;
    ui_clear_rect_fast(fb,hit);
    int date=s_stats.year*10000+s_stats.month*100+day;
    bool future=s_stats.summary.today_day&&date>s_stats.summary.today_day;
    uint64_t ms=s_stats.month_ms[day-1],min=ms/60000;
    uint8_t bg=future||!ms?0xFF:min<15?0xEE:min<45?0xBB:0x55;
    uint8_t ink=bg==0x55?0xFF:future?0xAA:FOLIO_INK;
    EpdRect visible={hit.x+8,hit.y+6,80,84};
    ui_fill_round_rect(fb,visible,10,bg);ui_draw_round_rect(fb,visible,10,FOLIO_RULE);
    if(day==s_stats.selected_day){
        ui_draw_round_rect(fb,(EpdRect){hit.x+3,hit.y+1,90,94},13,FOLIO_INK);
        ui_draw_round_rect(fb,(EpdRect){hit.x+4,hit.y+2,88,92},12,FOLIO_INK);
    }
    char text[24];snprintf(text,sizeof(text),"%d",day);
    folio_ui_text(fb,hit.x+48,hit.y+14,26,text,EPD_DRAW_ALIGN_CENTER,ink,bg);
    if(!future){
        if(!ms)snprintf(text,sizeof(text),"-");
        else if(!min)snprintf(text,sizeof(text),"<1");
        else snprintf(text,sizeof(text),"%llu",(unsigned long long)min);
        folio_ui_text(fb,hit.x+48,hit.y+46,23,text,EPD_DRAW_ALIGN_CENTER,ink,bg);
        int bars=!ms?0:min<15?1:min<45?2:3;
        for(int j=0;j<bars;j++)epd_fill_rect((EpdRect){hit.x+38,hit.y+78+j*3,20,1},ink,fb);
    }
}
static void draw_detail(uint8_t *fb){
    ui_clear_rect_fast(fb,(EpdRect){32,998,620,128});
    ui_fill_round_rect(fb,(EpdRect){32,998,620,128},20,FOLIO_SURFACE);
    char label[64];snprintf(label,sizeof(label),"%d月%d日%s",s_stats.month,s_stats.selected_day,selected_date()==s_stats.summary.today_day?" · 今日":"");
    folio_ui_text(fb,56,1014,28,label,EPD_DRAW_ALIGN_LEFT,FOLIO_INK,FOLIO_SURFACE);
    if(s_stats.selected_day)duration(fb,56,1062,32,s_stats.month_ms[s_stats.selected_day-1],EPD_DRAW_ALIGN_LEFT,FOLIO_SURFACE);
    folio_ui_chevron(fb,623,1066,false);
}
static void draw_month_header(uint8_t *fb){
    ui_clear_rect_fast(fb,(EpdRect){6,184,672,96});
    char month[48];snprintf(month,sizeof(month),"%d 年 %d 月",s_stats.year,s_stats.month);
    folio_ui_text(fb,342,211,36,month,EPD_DRAW_ALIGN_CENTER,FOLIO_INK,0xFF);
    if(s_stats.year>2020||s_stats.month>1)folio_ui_chevron(fb,60,232,true);
    int current=upper_date()/100;
    if(current && s_stats.year*100+s_stats.month<current)folio_ui_chevron(fb,621,232,false);
}
static void render_calendar(uint8_t *fb){
    folio_ui_header(fb,&s_stats.status,"阅读热力图",true);
    if(!s_stats.summary.today_day){
        folio_ui_text(fb,40,360,32,"热力图需要有效日期",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,442,28,"累计和单本时长仍然记录。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,494,26,"未归属日期的时长不会随意填入某个月。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_button(fb,(EpdRect){40,958,604,96},"查看已记录时长",false);
        folio_ui_button(fb,(EpdRect){40,1076,604,96},"设置日期",true);return;
    }
    draw_month_header(fb);
    static const char *labels[]={"0","<15","15-44","45+"};
    static const uint8_t grays[]={0xFF,0xEE,0xBB,0x55};
    for(int i=0;i<4;i++){
        EpdRect r={40+146*i,310,20,20};ui_fill_round_rect(fb,r,3,grays[i]);ui_draw_round_rect(fb,r,3,FOLIO_INK);
        folio_ui_text(fb,68+146*i,305,24,labels[i],EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    }
    folio_ui_text(fb,644,339,22,"分钟 / 格内数值为分钟",EPD_DRAW_ALIGN_RIGHT,FOLIO_SECONDARY,0xFF);
    static const char *week[]={"一","二","三","四","五","六","日"};
    for(int i=0;i<7;i++)folio_ui_text(fb,54+96*i,368,24,week[i],EPD_DRAW_ALIGN_CENTER,FOLIO_SECONDARY,0xFF);
    for(int d=1;d<=folio_ui_month_days(s_stats.year,s_stats.month);d++)draw_cell(fb,d);
    draw_detail(fb);
    if(!s_stats.summary.storage_ok||s_stats.summary.dirty||s_stats.summary.capacity_full)save_note(fb,1154);
    else folio_ui_text(fb,40,1154,24,"点击日期查看阅读时长",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
}
static int visible_book(int row){
    int n=0;
    for(size_t i=0;i<s_stats.summary.book_count;i++){
        if(s_stats.view==DAY&&!s_stats.book_day[i])continue;
        if(n++==s_stats.page*4+row)return (int)i;
    }
    return -1;
}
static int book_rows(void){int n=0;for(size_t i=0;i<s_stats.summary.book_count;i++)if(s_stats.view!=DAY||s_stats.book_day[i])n++;return n;}
static void render_books(uint8_t *fb){
    char title[64];
    snprintf(title,sizeof(title),s_stats.view==DAY?"%d月%d日的阅读":"单本书时长",s_stats.month,s_stats.selected_day);
    folio_ui_header(fb,&s_stats.status,title,true);
    duration(fb,40,230,48,s_stats.view==DAY?s_stats.month_ms[s_stats.selected_day-1]:s_stats.summary.total_ms,EPD_DRAW_ALIGN_LEFT,0xFF);
    folio_ui_text(fb,40,308,26,s_stats.view==DAY?"当日有效阅读时长":"累计有效阅读时长",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    if(!book_rows())folio_ui_text(fb,40,422,30,"暂无阅读记录",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    for(int r=0;r<4;r++){int i=visible_book(r);if(i<0)break;
        char value[48];folio_ui_duration(s_stats.view==DAY?s_stats.book_day[i]:s_stats.books[i].total_ms,value,sizeof(value));
        folio_ui_row(fb,398+r*124,s_stats.books[i].title,value,0xFF);
    }
    save_note(fb,916);
    folio_ui_button(fb,(EpdRect){40,964,190,96},"上一页",false);
    folio_ui_button(fb,(EpdRect){248,964,190,96},"计时规则",false);
    folio_ui_button(fb,(EpdRect){456,964,188,96},"下一页",false);
    folio_ui_button(fb,(EpdRect){40,1100,604,96},s_stats.view==DAY?"返回日期列表":"返回统计总览",false);
}
static void render_book(uint8_t *fb){
    folio_ui_header(fb,&s_stats.status,"单本阅读详情",true);
    folio_reading_book_t *book=&s_stats.books[s_stats.selected_book];
    folio_ui_text_fit(fb,40,226,38,604,book->title,FOLIO_INK,0xFF);
    folio_ui_text(fb,40,332,26,"此书累计",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    duration(fb,40,388,48,book->total_ms,EPD_DRAW_ALIGN_LEFT,0xFF);
    if(s_stats.book_parent==DAY){
        folio_ui_text(fb,40,540,26,"所选日期",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        duration(fb,40,596,40,s_stats.book_day[s_stats.selected_book],EPD_DRAW_ALIGN_LEFT,0xFF);
    }
    folio_ui_text(fb,40,824,26,"时间保存在本机，与阅读进度独立。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    save_note(fb,912);folio_ui_button(fb,(EpdRect){40,1100,604,96},"返回",false);
}
static void render_rules(uint8_t *fb){
    folio_ui_header(fb,&s_stats.status,"计时规则",true);
    const char *lines[]={"正文可见时累计阅读时间。","加载、目录、菜单和离页立即暂停。","默认连续闲置 5 分钟后暂停。","再次翻页或操作正文后继续计时。","休眠和关机期间不计时。","未校时的时长单列，不猜测日期。","不足一分钟的正时长显示 <1 分钟。","未保存记录可能在突然断电后丢失。","统计与阅读进度分别保存。"};
    for(size_t i=0;i<sizeof(lines)/sizeof(lines[0]);i++)folio_ui_text_fit(fb,40,230+(int)i*74,28,604,lines[i],i==7?FOLIO_INK:FOLIO_SECONDARY,0xFF);
    folio_ui_button(fb,(EpdRect){40,1100,604,96},"返回",false);
}
static void render_history(uint8_t *fb){
    folio_ui_header(fb,&s_stats.status,"阅读历史",true);
    if(!upper_date()){
        folio_ui_text(fb,40,240,30,"日期未设置 · 未归属日期",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        duration(fb,40,314,46,s_stats.summary.unknown_ms,EPD_DRAW_ALIGN_LEFT,0xFF);
        folio_ui_text(fb,40,438,26,"已保存的累计与单本记录仍可查看。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_button(fb,(EpdRect){40,566,604,96},"单本书时长",false);
    }else{
        draw_month_header(fb);
        for(int r=0;r<5;r++){
            int d=s_stats.page*5+r+1;if(d>folio_ui_month_days(s_stats.year,s_stats.month))break;
            if(s_stats.year*10000+s_stats.month*100+d>upper_date())break;
            char label[32],value[48];snprintf(label,sizeof(label),"%d月%d日",s_stats.month,d);folio_ui_duration(s_stats.month_ms[d-1],value,sizeof(value));
            folio_ui_row(fb,306+r*124,label,value,0xFF);
        }
        folio_ui_button(fb,(EpdRect){40,964,290,96},"上一页",false);folio_ui_button(fb,(EpdRect){346,964,298,96},"下一页",false);
    }
    folio_ui_button(fb,(EpdRect){40,1100,604,96},"返回统计总览",false);
}
static void render_months(uint8_t *fb){
    folio_ui_header(fb,&s_stats.status,"选择月份",true);
    char year[24];snprintf(year,sizeof(year),"%d 年",s_stats.picker_year);
    folio_ui_text(fb,342,216,38,year,EPD_DRAW_ALIGN_CENTER,FOLIO_INK,0xFF);
    if(s_stats.picker_year>2020)folio_ui_chevron(fb,60,236,true);
    if(s_stats.picker_year<s_stats.summary.today_day/10000)folio_ui_chevron(fb,621,236,false);
    for(int m=1;m<=12;m++){
        if(s_stats.picker_year*100+m>s_stats.summary.today_day/100)break;
        char label[20];snprintf(label,sizeof(label),"%d 月",m);
        folio_ui_button(fb,(EpdRect){40+(m-1)%3*208,350+(m-1)/3*124,188,96},label,s_stats.picker_year==s_stats.year&&m==s_stats.month);
    }
    folio_ui_button(fb,(EpdRect){40,1100,604,96},"返回月历",false);
}
static void render(app_ctx_t *ctx,uint8_t *fb){
    (void)ctx;
    switch(s_stats.view){case OVERVIEW:render_overview(fb);break;case CALENDAR:render_calendar(fb);break;
    case DAY:case BOOKS:render_books(fb);break;case BOOK:render_book(fb);break;case RULES:render_rules(fb);break;case HISTORY:render_history(fb);break;case MONTHS:render_months(fb);break;}
}
static app_redraw_t back(app_ctx_t *ctx){
    s_stats.dirty_count=0;
    if(s_stats.view==OVERVIEW){ctx->request_return=true;return APP_REDRAW_NONE;}
    if(s_stats.view==DAY){s_stats.view=s_stats.day_parent;s_stats.page=s_stats.day_parent_page;}
    else if(s_stats.view==BOOK){s_stats.view=s_stats.book_parent;s_stats.page=s_stats.book_parent_page;}
    else if(s_stats.view==RULES){s_stats.view=s_stats.rules_parent;s_stats.page=s_stats.rules_parent_page;}
    else if(s_stats.view==MONTHS){s_stats.view=CALENDAR;}
    else {s_stats.view=OVERVIEW;s_stats.page=0;}
    return APP_REDRAW_PAGE;
}
static bool shift_month(int delta){
    int y=s_stats.year,m=s_stats.month+delta;if(m==0){m=12;y--;}if(m==13){m=1;y++;}
    if(y<2020||!upper_date()||y*100+m>upper_date()/100)return false;
    s_stats.year=y;s_stats.month=m;s_stats.selected_day=1;s_stats.page=0;load_month();load_day();return true;
}
static app_redraw_t on_gesture(app_ctx_t *ctx,const ui_gesture_event_t *e){
    if(e->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    int x=e->x0,y=e->y0;
    if(ui_rect_hit((EpdRect){24,78,96,96},x,y))return back(ctx);
    if(s_stats.view!=CALENDAR&&ui_rect_hit((EpdRect){40,1100,604,96},x,y))return back(ctx);
    if(s_stats.view==OVERVIEW){
        if(ui_rect_hit((EpdRect){32,206,302,174},x,y)&&s_stats.summary.today_day){
            s_stats.year=s_stats.summary.today_day/10000;s_stats.month=s_stats.summary.today_day/100%100;s_stats.selected_day=s_stats.summary.today_day%100;load_month();load_day();s_stats.day_parent=OVERVIEW;s_stats.day_parent_page=0;s_stats.view=DAY;
        }else if(ui_rect_hit((EpdRect){346,206,306,174},x,y)||ui_rect_hit((EpdRect){40,976,290,96},x,y))s_stats.view=BOOKS;
        else if(ui_rect_hit((EpdRect){32,700,620,112},x,y))s_stats.view=CALENDAR;
        else if(ui_rect_hit((EpdRect){32,824,620,112},x,y))s_stats.view=HISTORY;
        else if(ui_rect_hit((EpdRect){346,976,298,96},x,y)){s_stats.rules_parent=OVERVIEW;s_stats.rules_parent_page=0;s_stats.view=RULES;}
        else if(!s_stats.summary.today_day&&ui_rect_hit((EpdRect){40,566,604,96},x,y)){ctx->request_app=&app_folio_datetime;return APP_REDRAW_NONE;}
        else return APP_REDRAW_NONE;
        s_stats.page=0;return APP_REDRAW_PAGE;
    }
    if(s_stats.view==CALENDAR){
        if(!s_stats.summary.today_day){
            if(ui_rect_hit((EpdRect){40,1076,604,96},x,y))ctx->request_app=&app_folio_datetime;
            if(ui_rect_hit((EpdRect){40,958,604,96},x,y)){s_stats.view=HISTORY;s_stats.page=0;return APP_REDRAW_PAGE;}
            return APP_REDRAW_NONE;
        }
        int delta=ui_rect_hit((EpdRect){6,184,96,96},x,y)?-1:ui_rect_hit((EpdRect){582,184,96,96},x,y)?1:0;
        if(delta&&shift_month(delta)){
            draw_month_header(ctx->fb);ui_clear_rect_fast(ctx->fb,(EpdRect){6,402,672,576});
            for(int d=1;d<=folio_ui_month_days(s_stats.year,s_stats.month);d++)draw_cell(ctx->fb,d);
            draw_detail(ctx->fb);s_stats.dirty[0]=(EpdRect){6,184,672,96};s_stats.dirty[1]=(EpdRect){6,402,672,576};s_stats.dirty[2]=(EpdRect){32,998,620,128};s_stats.dirty_count=3;return APP_REDRAW_AREA;
        }
        if(ui_rect_hit((EpdRect){156,184,372,96},x,y)){s_stats.picker_year=s_stats.year;s_stats.view=MONTHS;return APP_REDRAW_PAGE;}
        for(int d=1;d<=folio_ui_month_days(s_stats.year,s_stats.month);d++)if(ui_rect_hit(folio_ui_day_rect(s_stats.year,s_stats.month,d),x,y)){
            if(s_stats.year*10000+s_stats.month*100+d>s_stats.summary.today_day)return APP_REDRAW_NONE;
            int old=s_stats.selected_day;s_stats.dirty_count=folio_ui_selection_regions(s_stats.year,s_stats.month,old,d,s_stats.dirty);
            if(!s_stats.dirty_count)return APP_REDRAW_NONE;
            s_stats.selected_day=d;load_day();draw_cell(ctx->fb,old);draw_cell(ctx->fb,d);draw_detail(ctx->fb);return APP_REDRAW_AREA;
        }
        if(ui_rect_hit((EpdRect){32,998,620,128},x,y)){s_stats.day_parent=CALENDAR;s_stats.day_parent_page=0;s_stats.view=DAY;s_stats.page=0;return APP_REDRAW_PAGE;}
    }
    if(s_stats.view==MONTHS){
        if(ui_rect_hit((EpdRect){6,184,96,96},x,y)&&s_stats.picker_year>2020){s_stats.picker_year--;return APP_REDRAW_PAGE;}
        if(ui_rect_hit((EpdRect){582,184,96,96},x,y)&&s_stats.picker_year<s_stats.summary.today_day/10000){s_stats.picker_year++;return APP_REDRAW_PAGE;}
        for(int m=1;m<=12;m++)if(ui_rect_hit((EpdRect){40+(m-1)%3*208,350+(m-1)/3*124,188,96},x,y)&&s_stats.picker_year*100+m<=s_stats.summary.today_day/100){
            s_stats.year=s_stats.picker_year;s_stats.month=m;s_stats.selected_day=1;load_month();load_day();s_stats.view=CALENDAR;return APP_REDRAW_PAGE;
        }
    }
    if(s_stats.view==DAY||s_stats.view==BOOKS){
        for(int r=0;r<4;r++)if(ui_rect_hit((EpdRect){32,398+r*124,620,112},x,y)){
            int i=visible_book(r);if(i<0)return APP_REDRAW_NONE;s_stats.selected_book=i;s_stats.book_parent=s_stats.view;s_stats.book_parent_page=s_stats.page;s_stats.view=BOOK;return APP_REDRAW_PAGE;
        }
        if(ui_rect_hit((EpdRect){40,964,190,96},x,y)&&s_stats.page>0){s_stats.page--;return APP_REDRAW_PAGE;}
        if(ui_rect_hit((EpdRect){456,964,188,96},x,y)&&(s_stats.page+1)*4<book_rows()){s_stats.page++;return APP_REDRAW_PAGE;}
        if(ui_rect_hit((EpdRect){248,964,190,96},x,y)){s_stats.rules_parent=s_stats.view;s_stats.rules_parent_page=s_stats.page;s_stats.view=RULES;return APP_REDRAW_PAGE;}
    }
    if(s_stats.view==HISTORY){
        if(!upper_date()){if(ui_rect_hit((EpdRect){40,566,604,96},x,y)){s_stats.view=BOOKS;s_stats.page=0;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;}
        int delta=ui_rect_hit((EpdRect){6,184,96,96},x,y)?-1:ui_rect_hit((EpdRect){582,184,96,96},x,y)?1:0;
        if(delta&&shift_month(delta))return APP_REDRAW_PAGE;
        for(int r=0;r<5;r++)if(ui_rect_hit((EpdRect){32,306+r*124,620,112},x,y)){
            int d=s_stats.page*5+r+1;if(d>folio_ui_month_days(s_stats.year,s_stats.month)||s_stats.year*10000+s_stats.month*100+d>upper_date())return APP_REDRAW_NONE;
            s_stats.selected_day=d;load_day();s_stats.day_parent=HISTORY;s_stats.day_parent_page=s_stats.page;s_stats.view=DAY;s_stats.page=0;return APP_REDRAW_PAGE;
        }
        if(ui_rect_hit((EpdRect){40,964,290,96},x,y)&&s_stats.page>0){s_stats.page--;return APP_REDRAW_PAGE;}
        int max=folio_ui_month_days(s_stats.year,s_stats.month);if(s_stats.year*100+s_stats.month==upper_date()/100)max=upper_date()%100;
        if(ui_rect_hit((EpdRect){346,964,298,96},x,y)&&(s_stats.page+1)*5<max){s_stats.page++;return APP_REDRAW_PAGE;}
    }
    return APP_REDRAW_NONE;
}
static bool present(app_ctx_t *ctx,app_redraw_t redraw){
    if(redraw!=APP_REDRAW_AREA||!s_stats.dirty_count)return false;
    EpdRect area=s_stats.dirty[0];
    for(size_t i=1;i<s_stats.dirty_count;i++)area=ui_rect_union(area,s_stats.dirty[i]);
    guard_draw_result(ctx->hl,update_display_area_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,ui_rotate_rect_to_fb(area)));
    s_stats.dirty_count=0;return true;
}
static app_redraw_t on_key(app_ctx_t *ctx,int key){
    if(key==UI_KEY_1)return back(ctx);
    if(key==UI_KEY_2)return APP_REDRAW_FULL;
    if(key==UI_KEY_3)ctx->request_menu=true;
    return APP_REDRAW_NONE;
}
const app_desc_t app_folio_stats={.title="本地阅读统计",.detail="每日、单本与月历",.owns_keys=true,.owns_footer=true,
    .on_enter=on_enter,.render=render,.on_gesture=on_gesture,.on_key=on_key,.present=present,.on_tick=on_tick};
