/* SPDX-License-Identifier: Apache-2.0
 * 电源入口在停止文件传输、保存独立记录后才呈现最终锁屏。
 * Present the final lock frame only after stopping transfer and checkpointing independent records.
 */
#include "folio_power.h"
#include "folio_ui.h"
#include "reader_runtime.h"
#include "read_pico_transfer.h"
#include "read_pico_pmu.h"
#include "sleep.h"
#include "display.h"
#include "ui_kit.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int resolve_failure(app_ctx_t* ctx, bool transfer_busy, bool command_failed) {
    folio_status_t status; folio_ui_sample_status(&status);
    folio_ui_header(ctx->fb,&status,"暂未进入休眠",false);
    folio_ui_text(ctx->fb,40,226,36,transfer_busy ? "文件传输尚未结束" : command_failed ? "电源管理器未确认" : "记录保存未完成",EPD_DRAW_ALIGN_LEFT,FOLIO_INK,0xFF);
    folio_reader_save_status_t saved = folio_reader_save_status();
    folio_ui_row(ctx->fb,348,"阅读进度",saved.progress_ok ? "已保存" : "待处理",0xFF);
    folio_ui_row(ctx->fb,474,"阅读统计",saved.statistics_ok ? "已保存" : "待处理",0xFF);
    folio_ui_row(ctx->fb,600,"排版草稿",saved.draft_ok ? "已保存" : "待处理",0xFF);
    folio_ui_text_fit(ctx->fb,40,764,28,604,"当前内存内容仍保留，未执行断电。",FOLIO_SECONDARY,0xFF);
    const char* reason = folio_reader_blocked_reason();
    folio_ui_text_fit(ctx->fb,40,818,26,604,reason[0] ? reason : "可以重试，或返回处理存储和编辑状态。",FOLIO_SECONDARY,0xFF);
    folio_ui_button(ctx->fb,(EpdRect){40,968,284,96},"返回编辑",false);
    folio_ui_button(ctx->fb,(EpdRect){360,968,284,96},"重试",true);
    guard_draw_result(ctx->hl,update_display_mode(ctx->hl,MODE_GL16));
    app_lock_wait_key_idle(800);
    bool armed = false, held = false; int choice = -1;
    for (;;) {
        if (read_pico_pmu_take_key_short()) return -1;
        cst836u_touch_t touch = {0};
        esp_err_t touch_error = ctx->tp ? cst836u_read(ctx->tp,&touch) : ESP_ERR_INVALID_STATE;
        if (touch_error == ESP_OK) {
            if (!touch.touched && !held) armed = true;
            if (armed && touch.touched && !held) {
                choice = ui_rect_hit((EpdRect){360,968,284,96},touch.x,touch.y) ? 1 :
                         ui_rect_hit((EpdRect){40,968,284,96},touch.x,touch.y) ? 0 : -1;
            }
            if (touch.touched && choice >= 0) {
                EpdRect hit = choice ? (EpdRect){360,968,284,96} : (EpdRect){40,968,284,96};
                if (!ui_rect_hit(hit,touch.x,touch.y)) choice = -1;
            }
            if (held && !touch.touched && choice >= 0) return choice;
            if (touch.count > 1) choice = -1;
            held = touch.touched;
        } else {
            armed = false; held = false; choice = -1;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
static bool retry_or_return(app_ctx_t* ctx, bool transfer_busy, bool command_failed) {
    for (;;) {
        int choice = resolve_failure(ctx,transfer_busy,command_failed);
        if (choice != 0 || !folio_reader_has_unapplied_draft()) return choice == 1;
        if (folio_reader_return_to_editor(ctx)) return false;
        // 缺少原书/字体时留在此处，恢复入口和内存草稿仍在。
        // Missing original media/font keeps this screen, the recovery entry and RAM draft intact.
    }
}
void folio_power_request(app_ctx_t* ctx, bool poweroff, int64_t* ignore_until_ms) {
    app_sleep_mode_t mode = poweroff ? APP_SLEEP_OFF : app_settings_sleep_mode();
    bool eco = mode != APP_SLEEP_LIGHT;
    for (;;) {
        bool network_stopped = read_pico_transfer_try_stop_if_idle();
        bool saved = false;
        int64_t start = esp_timer_get_time();
        // 最多两次完整保存；超两秒不发起下一次，不中止正在完成的文件写入。
        // At most two saves; start no further attempt after two seconds, never interrupt an in-flight file write.
        for (int attempt = 0; network_stopped && attempt < 2; ++attempt) {
            saved = folio_reader_prepare_sleep(eco);
            if (saved || esp_timer_get_time()-start >= 2000000) break;
        }
        if (!network_stopped || (eco && !saved)) {
            if (retry_or_return(ctx,!network_stopped,false)) continue;
            break;
        }
        if (enter_lock_and_sleep_mode(ctx->hl,ignore_until_ms,ctx->acc,mode)) break;
        if (!retry_or_return(ctx,false,true)) break;
    }
    folio_reader_wake();
}
