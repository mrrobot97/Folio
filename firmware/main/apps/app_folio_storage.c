/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：内置 FAT 状态、无格式化重试，以及显式两阶段初始化确认。
 * English: Flash FAT status, non-formatting retry and explicit two-stage initialization consent.
 * 冻结：render/进页绝不格式化；仅最终确认动作调用初始化；不操作 TF/NVS。
 * Frozen: Never format on render/entry; only final confirmation initializes; never touch TF/NVS.
 */
#include "app.h"
#include "folio_ui.h"
#include "folio_navigation.h"
#include "book_store.h"
#include "reader_runtime.h"
#include "ui_kit.h"
#include "ui_gesture.h"
#include "ui_menu.h"
#include <stdio.h>
#include <string.h>

typedef enum { STORAGE_STATUS, STORAGE_WARNING, STORAGE_CONFIRM } storage_view_t;
static struct {folio_status_t status;storage_view_t view;bool ready;uint64_t free_bytes;esp_err_t error;char message[128];} s_storage;
static void sample(void){
    folio_ui_sample_status(&s_storage.status);
    s_storage.ready=book_store_flash_ready();s_storage.free_bytes=0;
    if(s_storage.ready){book_store_root_t root={.path="/flash/books",.is_flash=true};s_storage.free_bytes=book_store_free_bytes(&root);}
}
static void on_enter(app_ctx_t *ctx){(void)ctx;s_storage.view=STORAGE_STATUS;s_storage.message[0]=0;sample();}
static void render(app_ctx_t *ctx,uint8_t *fb){
    (void)ctx;
    folio_ui_header(fb,&s_storage.status,s_storage.view==STORAGE_STATUS?"内置存储":"初始化内置存储",true);
    if(s_storage.view==STORAGE_STATUS){
        folio_ui_text(fb,40,228,38,s_storage.ready?"内置存储可用":"内置存储未就绪",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        if(s_storage.ready){
            char capacity[80];snprintf(capacity,sizeof(capacity),"可用 %llu KiB / 分区 5 MiB",(unsigned long long)(s_storage.free_bytes/1024));
            folio_ui_text(fb,40,308,28,capacity,EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
            folio_ui_text(fb,40,384,28,"图书、阅读统计与排版草稿共用此空间。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
            folio_ui_text(fb,40,448,28,"可用空间会扣除文件系统与保留空间。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        }else{
            folio_ui_text(fb,40,308,28,"可能尚未初始化，或文件系统已损坏。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
            folio_ui_text(fb,40,384,28,"先尝试重新挂载，保留已有数据。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
            folio_ui_text(fb,40,448,28,"若仍失败，可备份后再选择初始化。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        }
        folio_ui_button(fb,(EpdRect){40,574,604,96},"重新读取 / 尝试挂载",false);
        if(!s_storage.ready)folio_ui_button(fb,(EpdRect){40,714,604,96},"初始化内置存储…",false);
        if(s_storage.message[0])folio_ui_text_fit(fb,40,866,28,604,s_storage.message,FOLIO_INK,0xFF);
        folio_ui_text(fb,40,974,26,"此页不会自动擦除任何存储。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
    }else{
        bool final=s_storage.view==STORAGE_CONFIRM;
        folio_ui_text(fb,40,226,38,final?"确认初始化与清除风险":"初始化可能清除内置数据",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
        const char *lines[]={"内置书库中的 TXT / EPUB 图书", "内置阅读统计与日期历史", "内置排版草稿及检查点", "不会清除 TF 卡或 NVS 设置", "清除后无法在设备上撤销"};
        for(int i=0;i<5;i++)folio_ui_text(fb,40,338+i*78,28,lines[i],EPD_DRAW_ALIGN_LEFT,i==4?FOLIO_INK:FOLIO_SECONDARY,0xFF);
        folio_ui_text(fb,40,794,26,final?"初始化失败的分区时，可能清除上述数据。":"不确定时请取消，先检查或备份数据。",EPD_DRAW_ALIGN_LEFT,FOLIO_SECONDARY,0xFF);
        folio_ui_button(fb,(EpdRect){40,942,604,96},final?"确认初始化（可能清除）":"我已了解，继续",true);
    }
    folio_ui_button(fb,(EpdRect){40,1100,604,96},s_storage.view==STORAGE_STATUS?"返回":"取消，保留数据",false);
}
static app_redraw_t back(app_ctx_t *ctx){
    if(s_storage.view!=STORAGE_STATUS){s_storage.view=STORAGE_STATUS;return APP_REDRAW_PAGE;}
    ctx->request_return=true;return APP_REDRAW_NONE;
}
static app_redraw_t on_gesture(app_ctx_t *ctx,const ui_gesture_event_t *e){
    if(e->type!=UI_GESTURE_TAP)return APP_REDRAW_NONE;
    int x=e->x0,y=e->y0;
    if(ui_rect_hit((EpdRect){24,78,96,96},x,y)||ui_rect_hit((EpdRect){40,1100,604,96},x,y))return back(ctx);
    if(s_storage.view==STORAGE_STATUS){
        if(ui_rect_hit((EpdRect){40,574,604,96},x,y)){
            book_store_root_t roots[2];int count=0;s_storage.error=book_store_roots(roots,&count);sample();
            bool recovered=s_storage.ready&&folio_reader_retry_storage();
            snprintf(s_storage.message,sizeof(s_storage.message),recovered?"存储与阅读记录已恢复":s_storage.ready?"已挂载，记录恢复失败；RAM 与原文件保留":"内置存储仍未就绪，原数据未擦除");return APP_REDRAW_PAGE;
        }
        if(!s_storage.ready&&ui_rect_hit((EpdRect){40,714,604,96},x,y)){s_storage.view=STORAGE_WARNING;return APP_REDRAW_PAGE;}
    }else if(ui_rect_hit((EpdRect){40,942,604,96},x,y)){
        if(s_storage.view==STORAGE_WARNING){s_storage.view=STORAGE_CONFIRM;return APP_REDRAW_PAGE;}
        // 底层也拒绝已挂载分区，防止确认期间状态变化误擦。/ Backend also rejects mounted storage if state changed during consent.
        if(book_store_flash_ready()){
            snprintf(s_storage.message,sizeof(s_storage.message),"存储已挂载，已取消初始化");
        }else{
            s_storage.error=book_store_initialize_flash(true);
            if(s_storage.error==ESP_OK){
                bool recovered=folio_reader_storage_reinitialized();
                snprintf(s_storage.message,sizeof(s_storage.message),recovered?"初始化成功，内置存储已就绪":"存储已初始化，阅读记录服务恢复失败");
            }else snprintf(s_storage.message,sizeof(s_storage.message),"初始化失败，请保留设备并检查存储");
        }
        sample();s_storage.view=STORAGE_STATUS;return APP_REDRAW_PAGE;
    }
    return APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t *ctx,int key){
    if(key==UI_KEY_1)return back(ctx);
    if(key==UI_KEY_2)return APP_REDRAW_FULL;
    if(key==UI_KEY_3)ctx->request_menu=true;
    return APP_REDRAW_NONE;
}
const app_desc_t app_folio_storage={.title="内置存储",.detail="状态与显式初始化",.owns_keys=true,.owns_footer=true,
    .on_enter=on_enter,.render=render,.on_gesture=on_gesture,.on_key=on_key};
