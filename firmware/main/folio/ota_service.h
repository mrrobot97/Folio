/* SPDX-License-Identifier: Apache-2.0
 * IDF OTA 传输和健康确认，永不自动重启。/ IDF OTA transfer and health checks; never reboot automatically.
 */
#pragma once
#include "ota_policy.h"
#include "esp_err.h"
#define FOLIO_OTA_HEALTH_DISPLAY (1u << 0)
#define FOLIO_OTA_HEALTH_INPUT (1u << 1)
#define FOLIO_OTA_HEALTH_SETTINGS (1u << 2)
#define FOLIO_OTA_HEALTH_STORAGE (1u << 3)
#define FOLIO_OTA_HEALTH_LOOP (1u << 4)
#define FOLIO_OTA_HEALTH_ALL ((1u << 5) - 1u)

typedef struct {
    const char* board;
    size_t bootloader_size;
    uint8_t bootloader_sha256[32];
    bool physical_board_verified;
    bool installed_bootloader_rollback_verified;
} folio_ota_provisioning_t;

typedef struct {
    const char* version; ///< 目标 x.y.z；固定 GitHub 仓库。/ Target x.y.z; fixed GitHub repository.
    void* context;
    /// 只能返回先前显式审核的置备证据；不得从下载清单或编译宏推断。
    /// Return previously explicitly audited provisioning evidence; never infer from manifest/build macros.
    bool (*provisioning)(void* context, folio_ota_provisioning_t* evidence);
    /// 必须刚成功保存阅读进度/统计/草稿。/ Must successfully checkpoint progress/statistics/draft first.
    bool (*checkpoint)(void* context);
    /// 重新读取电池，失败返回 false。/ Read fresh battery; false on failure.
    bool (*battery)(void* context, unsigned* permille);
    /// 用户取消或要求睡眠时返回 true。/ True on user cancellation or sleep request.
    bool (*cancelled)(void* context);
    /// 工作线程回调，不可直接绘制。/ Worker callback; no direct UI rendering.
    void (*progress)(void* context, size_t received, size_t total);
} folio_ota_request_t;

/// 严格匹配可选布局；factory 固件恒为 false。/ Match optional layout; factory firmware returns false.
bool folio_ota_layout_available(void);
/// 同步、独占调用；由 UI 在专用任务启动并抑制休眠，返回后关闭按需 WiFi。
/// Synchronous exclusive call; UI runs on worker, inhibits sleep and stops on-demand WiFi on return.
esp_err_t folio_ota_install(const folio_ota_request_t* request, folio_ota_result_t* refusal);
/// 在首屏、触摸、设置/内部数据恢复和事件循环成功后调用；缺项不确认。
/// Call after display, input, settings/internal-data recovery and event loop pass; missing checks refuse.
esp_err_t folio_ota_confirm_boot(unsigned health_checks);
