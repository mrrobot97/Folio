/*
 * SPDX-License-Identifier: Apache-2.0
 * 可选软件边界观测；不记录用户数据，不表示面板物理响应或电流。
 * Optional software-boundary observations; no user data, optical latency or current.
 */
#pragma once

#include <stdint.h>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

typedef enum {
    FOLIO_METRICS_SYSTEM,   ///< 启动与系统 / Startup and system
    FOLIO_METRICS_HOME,     ///< 首页 / Home
    FOLIO_METRICS_LIBRARY,  ///< 书架 / Library
    FOLIO_METRICS_READER,   ///< 阅读 / Reading
    FOLIO_METRICS_FORMAT,   ///< 排版编辑 / Format editing
    FOLIO_METRICS_STATS,    ///< 统计 / Statistics
    FOLIO_METRICS_SETTINGS, ///< 设置 / Settings
    FOLIO_METRICS_TRANSFER, ///< 传书 / Transfer
    FOLIO_METRICS_SLEEP,    ///< 睡眠 / Sleep
    FOLIO_METRICS_OTHER     ///< 其他 / Other
} folio_metrics_scenario_t;

typedef enum {
    FOLIO_METRICS_TOUCH,   ///< 触摸 / Touch
    FOLIO_METRICS_KEY,     ///< 按键 / Key
    FOLIO_METRICS_GESTURE, ///< 手势 / Gesture
    FOLIO_METRICS_POWER    ///< 电源键 / Power key
} folio_metrics_input_kind_t;

typedef enum {
    FOLIO_METRICS_WAVE_UNKNOWN, ///< 未知波形 / Unknown waveform
    FOLIO_METRICS_WAVE_PANEL,   ///< 官方面板波形 / Official panel waveform
    FOLIO_METRICS_WAVE_FOLLOW   ///< 官方跟随波形 / Official follow waveform
} folio_metrics_wave_t;

/// 开始边界的不可变快照，允许预排版任务延后结束。/ Immutable start snapshot for asynchronous completion.
typedef struct {
    int64_t begin_us;                    ///< 单调微秒起点 / Monotonic start in microseconds
    uint64_t input_id;                   ///< 输入编号，0 表示后台 / Input ID, 0 for background
    folio_metrics_scenario_t scenario;   ///< 开始时的固定场景 / Fixed scenario at start
} folio_metrics_span_t;

#if defined(CONFIG_FOLIO_METRICS) && CONFIG_FOLIO_METRICS
/// 启动时一次调用，先于其他采样；禁止 ISR。/ Call once at startup before sampling; no ISR use.
void folio_metrics_init(void);
/// 切页后设置背景任务场景，不改变已经开始的 span。/ Set background scenario after navigation, leaving existing spans intact.
void folio_metrics_set_scenario(folio_metrics_scenario_t scenario);
/// 已去抖/识别的事件派发前记录；输入串行，不嵌套。/ Record before debounced/recognized input dispatch; serial, not nested.
void folio_metrics_input(folio_metrics_scenario_t scenario, folio_metrics_input_kind_t kind);
/// 完成该事件所有同步处理后关闭关联，包括无刷新的事件。/ End correlation after all synchronous handling, including no-redraw events.
void folio_metrics_input_end(void);
/// 只读时钟和短临界区，不写日志；不在 ISR 中调用。/ Clock read and short critical section only; no log or ISR use.
folio_metrics_span_t folio_metrics_begin(void);
/// 结束时先取时钟，再采样堆与写日志。/ Capture end time before heap sampling and logging.
void folio_metrics_render_end(folio_metrics_span_t span);
/// mode 为最终 EpdDrawMode，区域为请求坐标；result 保留驱动返回值。
/// mode is the final EpdDrawMode; area is requested coordinates; result is the driver return value.
void folio_metrics_display_end(folio_metrics_span_t span, uint32_t mode,
    folio_metrics_wave_t wave, int x, int y, int w, int h, int result);
#else
// 关闭时不求值实参，不读时钟/堆/锁，不产生日志。/ Disabled: no argument evaluation, clock, heap, lock or log.
#define folio_metrics_init() ((void)0)
#define folio_metrics_set_scenario(scenario) ((void)sizeof(scenario))
#define folio_metrics_input(scenario, kind) ((void)sizeof(scenario), (void)sizeof(kind))
#define folio_metrics_input_end() ((void)0)
#define folio_metrics_begin() ((folio_metrics_span_t){0})
#define folio_metrics_render_end(span) ((void)sizeof(span))
#define folio_metrics_display_end(span, mode, wave, x, y, w, h, result) \
    ((void)sizeof(span), (void)sizeof(mode), (void)sizeof(wave), (void)sizeof(x), \
     (void)sizeof(y), (void)sizeof(w), (void)sizeof(h), (void)sizeof(result))
#endif
