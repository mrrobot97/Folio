/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 锁屏进睡、浅睡等待按键或拿起、软睡/关机拉掉 EN。
 *
 * Enter lock and sleep, light-sleep wait for key or pickup, and drop EN
 * for soft sleep / power-off.
 */

#include "sleep.h"

#include "app.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pmu_selftest.h"
#include "read_pico_board.h"
#include "read_pico_pmu.h"
#include "sc7a20h_lab.h"
#include "settings.h"
#include "ui_kit.h"
#include "reader_runtime.h"
#include "format_service.h"
#include "read_pico_transfer.h"
#include "wallpaper.h"
#include <stdlib.h>

static const char* TAG = "read_pico";

extern const uint8_t lock_4bpp_bin_start[] asm("_binary_lock_4bpp_bin_start");

static void lock_arm_ioe_wakeup(void) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << READ_PICO_IOE_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    read_pico_clear_ioe_int();
    gpio_wakeup_enable((gpio_num_t)READ_PICO_IOE_INT_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

void app_lock_wait_key_idle(int timeout_ms) {
    int64_t start = esp_timer_get_time() / 1000;
    int64_t high_from = 0;
    while (esp_timer_get_time() / 1000 - start < timeout_ms) {
        read_pico_pmu_drain_events();
        read_pico_clear_ioe_int();
        bool held = false;
        if (read_pico_pmu_poll() == ESP_OK) {
            held = (read_pico_pmu_get()->key_state & 0x01) != 0;
        }
        bool int_high = gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO) != 0;
        if (!held && int_high) {
            if (high_from == 0) high_from = esp_timer_get_time() / 1000;
            if (esp_timer_get_time() / 1000 - high_from >= 150) return;
        } else {
            high_from = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

app_wake_source_t app_last_wake_source(void) {
    return (app_wake_source_t)app_settings_last_wake();
}

static void pickup_ack(sc7a20h_handle_t acc) {
    sc7a20h_events_t ev;
    sc7a20h_read_events(acc, &ev);
    sc7a20h_ack_int(acc);
}

static bool pickup_ia(sc7a20h_handle_t acc) {
    sc7a20h_events_t ev;
    sc7a20h_aoi_src_t aoi;
    if (sc7a20h_read_events(acc, &ev) != ESP_OK) return false;
    sc7a20h_aoi_decode(ev.aoi1_src, &aoi);
    return aoi.ia;
}

// 第一次低电平不够：高通余波还会再拉高。要连续安静一段时间才进浅睡。
// A first low is not enough: high-pass ringing can rise again. Wait for a quiet stretch before light sleep.
static bool pickup_wait_quiet(sc7a20h_handle_t acc, int quiet_ms, int timeout_ms) {
    int64_t start = esp_timer_get_time() / 1000;
    int64_t low_from = 0;
    while (esp_timer_get_time() / 1000 - start < timeout_ms) {
        if (sc7a20h_int1_level(acc) == 0) {
            if (low_from == 0) low_from = esp_timer_get_time() / 1000;
            if (esp_timer_get_time() / 1000 - low_from >= quiet_ms) return true;
        } else {
            pickup_ack(acc);
            low_from = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc) {
    bool pickup = acc != NULL && app_settings_pickup_wake();
    bool acc_armed = false;
    lock_arm_ioe_wakeup();
    if (pickup) {
        sc7a20h_motion_cfg_t motion = SC7A20H_MOTION_DEFAULT();
        motion.ths_mg = 350;
        motion.duration = 3;
        sc7a20h_arm_pickup_wake(acc, &motion);
        acc_armed = true;
        pickup_ack(acc);
        if (!pickup_wait_quiet(acc, 400, 2000)) {
            pickup = false;
            ESP_LOGW(TAG, "pickup INT1 stuck high, key only");
        } else {
            sc7a20h_config_light_sleep_wakeup(acc);
            if (!pickup_wait_quiet(acc, 400, 1500)) {
                gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                pickup = false;
                ESP_LOGW(TAG, "pickup INT1 re-asserted, key only");
            } else {
                // 安静窗口里若 INT1 闪过，ESP 会留下高电平唤醒挂起，关掉再打开清掉。
                // If INT1 glitched in the quiet window, ESP keeps a high-level wake pending; disable then re-enable to clear it.
                gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                sc7a20h_config_light_sleep_wakeup(acc);
                if (sc7a20h_int1_level(acc) > 0) {
                    gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                    pickup = false;
                    ESP_LOGW(TAG, "pickup INT1 high at sleep, key only");
                } else {
                    ESP_LOGI(
                        TAG, "wait key GPIO%d or pickup GPIO%d",
                        READ_PICO_IOE_INT_GPIO, (int)sc7a20h_int1_gpio(acc)
                    );
                }
            }
        }
    } else {
        ESP_LOGI(TAG, "wait key on IOE_INT GPIO%d", READ_PICO_IOE_INT_GPIO);
    }

    app_wake_source_t wake = APP_WAKE_NONE;
    bool slept = false;
    for (;;) {
        read_pico_clear_ioe_int();
        if (!slept && gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO) == 0) {
            read_pico_pmu_drain_events();
            read_pico_clear_ioe_int();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!slept && pickup && sc7a20h_int1_level(acc) > 0) {
            pickup_ack(acc);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (slept && gpio_get_level((gpio_num_t)READ_PICO_IOE_INT_GPIO) == 0) {
            if (read_pico_pmu_take_key_wakeup()) {
                wake = APP_WAKE_KEY;
                break;
            }
            read_pico_clear_ioe_int();
            continue;
        }
        if (slept && pickup && sc7a20h_int1_level(acc) > 0 && pickup_ia(acc)) {
            wake = APP_WAKE_PICKUP;
            ESP_LOGI(TAG, "pickup wake");
            break;
        }
        if (slept && pickup && sc7a20h_int1_level(acc) > 0) {
            pickup_ack(acc);
            pickup_wait_quiet(acc, 400, 1500);
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        esp_light_sleep_start();
        slept = true;
        int64_t dt_ms = (esp_timer_get_time() - t0) / 1000;
        read_pico_clear_ioe_int();
        if (read_pico_pmu_take_key_wakeup()) {
            wake = APP_WAKE_KEY;
            break;
        }
        if (pickup && sc7a20h_int1_level(acc) > 0 && pickup_ia(acc)) {
            if (dt_ms < 80) {
                ESP_LOGW(TAG, "pickup ignored, sleep %lld ms", (long long)dt_ms);
                pickup_ack(acc);
                pickup_wait_quiet(acc, 400, 1500);
                continue;
            }
            wake = APP_WAKE_PICKUP;
            ESP_LOGI(TAG, "pickup wake");
            break;
        }
        if (pickup && sc7a20h_int1_level(acc) > 0) {
            pickup_ack(acc);
            pickup_wait_quiet(acc, 400, 1500);
        }
    }

    gpio_wakeup_disable((gpio_num_t)READ_PICO_IOE_INT_GPIO);
    if (acc_armed) {
        gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
        sc7a20h_events_t ev;
        sc7a20h_read_events(acc, &ev);
        sc7a20h_power_down(acc);
        sc7a20h_int1_begin(acc);
    }
    app_settings_set_last_wake((uint8_t)wake);
    return wake;
}

static bool host_sleep_after_checkpoint(app_sleep_mode_t mode) {
    if (!read_pico_transfer_try_stop_if_idle()) return false;
    pmu_selftest_prepare_powerdown();
    esp_err_t err;
    if (mode == APP_SLEEP_OFF) {
        err = read_pico_pmu_power_off();
    } else {
        err = read_pico_pmu_report_sleep();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "powerdown command rejected: %s", esp_err_to_name(err));
        folio_reader_wake();
        return false;
    }
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

bool app_enter_host_sleep(app_sleep_mode_t mode) {
    folio_format_capture_power_origin(NULL);
    if (!read_pico_transfer_try_stop_if_idle() || !folio_reader_prepare_sleep(true)) {
        ESP_LOGW(TAG, "powerdown aborted: transfer or checkpoint incomplete");
        folio_reader_wake();
        return false;
    }
    return host_sleep_after_checkpoint(mode);
}

void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc
) {
    folio_format_capture_power_origin(NULL);
    app_sleep_mode_t mode = app_settings_sleep_mode();
    if (!read_pico_transfer_try_stop_if_idle()) return;
    bool saved = folio_reader_prepare_sleep(mode != APP_SLEEP_LIGHT);
    if (mode == APP_SLEEP_LIGHT || saved) (void)enter_lock_and_sleep_mode(hl,ignore_until_ms,acc,mode);
    folio_reader_wake();
}

bool enter_lock_and_sleep_mode(EpdiyHighlevelState* hl, int64_t* ignore_until_ms,
                              sc7a20h_handle_t acc, app_sleep_mode_t mode) {
    uint8_t* framebuffer = epd_hl_get_framebuffer(hl);
    uint8_t* custom = NULL;
    const char* path = app_settings_wallpaper_path();
    bool custom_ok = path[0] && folio_wallpaper_read(path,&custom);
    ui_draw_full_image(framebuffer,custom_ok ? custom : lock_4bpp_bin_start);
    free(custom);
    // 保留高层参考帧，按正常 GL16/周期 GC16 推一次，避免进出各先刷白。
    // Keep the high-level reference and present once with normal GL16/periodic GC16, avoiding entry/exit white wipes.
    epd_fill_rect((EpdRect){0,1050,684,166},0xFF,framebuffer);
    const char* label = mode == APP_SLEEP_LIGHT ? "FAST · 内存保留" : mode == APP_SLEEP_OFF ? "准备关机" : "准备进入 ECO";
    ui_text(framebuffer,342,1070,32,label,EPD_DRAW_ALIGN_CENTER,false);
    folio_reader_save_status_t saved = folio_reader_save_status();
    const char* status = saved.progress_ok && saved.statistics_ok && saved.draft_ok ? "记录已保存" : "仍有记录未保存 · 内存中保留";
    ui_text(framebuffer,342,1120,26,status,EPD_DRAW_ALIGN_CENTER,false);
    if (path[0] && !custom_ok)
        ui_text(framebuffer,342,1160,24,"壁纸不可用 · 已使用内置壁纸",EPD_DRAW_ALIGN_CENTER,false);
    guard_draw_result(hl,update_display_mode(hl,MODE_GL16));

    app_lock_wait_key_idle(800);
    ESP_LOGI(TAG, "lock %s", app_sleep_mode_name(mode));

    switch (mode) {
        case APP_SLEEP_OFF:
        case APP_SLEEP_DEEP:
            if (!saved.progress_ok || !saved.statistics_ok || !saved.draft_ok) return false;
            if (!host_sleep_after_checkpoint(mode)) return false;
            break;
        case APP_SLEEP_LIGHT:
        default:
            epd_poweroff();
            app_light_sleep_wait(acc);
            break;
    }

    // 屏与参考帧仍是锁屏图，主循环直接差分画回原页。
    // Panel and reference still contain the lock image; the loop draws the original page directly.
    read_pico_pmu_drain_events();
    if (ignore_until_ms) {
        *ignore_until_ms = esp_timer_get_time() / 1000 + APP_LOCK_IGNORE_BOOT_MS;
    }
    ESP_LOGI(TAG, "unlocked");
    return true;
}
