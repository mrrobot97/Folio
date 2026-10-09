/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 锁屏、浅睡等待、软睡/关机下电。
 *
 * Lock, light-sleep wait, and soft-sleep / power-off rail drop.
 */

#pragma once

#include <stdint.h>

#include "epd_highlevel.h"
#include "sc7a20h.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_WAKE_NONE = 0,
    APP_WAKE_KEY,
    APP_WAKE_PICKUP,
} app_wake_source_t;

void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc
);
/// 调用方刚完成串行保存后指定模式；不重复写检查点，仅浅睡唤醒返回 true，失败 false。
/// Explicit mode after a fresh serialized checkpoint; never repeat the write, true on light wake, false on failure.
bool enter_lock_and_sleep_mode(EpdiyHighlevelState* hl, int64_t* ignore_until_ms,
                              sc7a20h_handle_t acc, app_sleep_mode_t mode);

/// 等电源键松开，避免进睡瞬间被同一下按住立刻唤醒。
/// Wait for the power key to release so the same press does not wake immediately.
void app_lock_wait_key_idle(int timeout_ms);
/// ESP 浅睡，按键或拿起唤醒。acc 为空则只等按键。
/// ESP light sleep; wake on key or pickup. Key only when acc is NULL.
app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc);
app_wake_source_t app_last_wake_source(void);
/// 保存成功后才软睡/关机；失败返回 false，成功切断 EN 后不返回。
/// Soft sleep/power-off only after saving; false on failure, no return after successful EN drop.
bool app_enter_host_sleep(app_sleep_mode_t mode);

#ifdef __cplusplus
}
#endif
