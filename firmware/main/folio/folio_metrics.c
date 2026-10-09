/*
 * SPDX-License-Identifier: Apache-2.0
 * 固定标签的 JSONL 观测，不分配内存；仅短暂锁住关联状态。
 * Fixed-tag JSONL observations without allocation; only correlation state is locked briefly.
 */
#include "folio_metrics.h"

#if defined(CONFIG_FOLIO_METRICS) && CONFIG_FOLIO_METRICS
#include <inttypes.h>
#include <stddef.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char* TAG = "folio_metrics";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_sequence;
static uint64_t s_input_id;
static folio_metrics_scenario_t s_scenario;

static const char* scenario_name(folio_metrics_scenario_t scenario) {
    static const char* const names[] = {
        "system", "home", "library", "reader", "format", "stats",
        "settings", "transfer", "sleep", "other"
    };
    return (unsigned)scenario < sizeof(names) / sizeof(names[0]) ? names[scenario] : "other";
}

static const char* input_name(folio_metrics_input_kind_t kind) {
    static const char* const names[] = {"touch", "key", "gesture", "power"};
    return (unsigned)kind < sizeof(names) / sizeof(names[0]) ? names[kind] : "unknown";
}

static const char* wave_name(folio_metrics_wave_t wave) {
    static const char* const names[] = {"unknown", "panel", "follow"};
    return (unsigned)wave < sizeof(names) / sizeof(names[0]) ? names[wave] : "unknown";
}

static uint64_t next_sequence(void) {
    portENTER_CRITICAL(&s_lock);
    const uint64_t seq = ++s_sequence;
    portEXIT_CRITICAL(&s_lock);
    return seq;
}

void folio_metrics_init(void) {
    // 初始化仅在工作任务启动前执行。/ Initialization occurs before worker startup.
    s_sequence = 0;
    s_input_id = 0;
    s_scenario = FOLIO_METRICS_SYSTEM;
    ESP_LOGI(TAG, "{\"v\":1,\"seq\":%" PRIu64 ",\"kind\":\"session\",\"ts_us\":%" PRId64 "}",
        next_sequence(), esp_timer_get_time());
}

void folio_metrics_set_scenario(folio_metrics_scenario_t scenario) {
    portENTER_CRITICAL(&s_lock);
    s_scenario = scenario;
    portEXIT_CRITICAL(&s_lock);
}

void folio_metrics_input(folio_metrics_scenario_t scenario, folio_metrics_input_kind_t kind) {
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    const uint64_t seq = ++s_sequence;
    s_input_id = seq;
    s_scenario = scenario;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "{\"v\":1,\"seq\":%" PRIu64 ",\"kind\":\"input\",\"ts_us\":%" PRId64
        ",\"input_id\":%" PRIu64 ",\"scenario\":\"%s\",\"input\":\"%s\"}",
        seq, now, seq, scenario_name(scenario), input_name(kind));
}

void folio_metrics_input_end(void) {
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    const uint64_t id = s_input_id;
    const folio_metrics_scenario_t scenario = s_scenario;
    s_input_id = 0;
    const uint64_t seq = id ? ++s_sequence : 0;
    portEXIT_CRITICAL(&s_lock);
    if (id) ESP_LOGI(TAG, "{\"v\":1,\"seq\":%" PRIu64 ",\"kind\":\"input_end\",\"ts_us\":%" PRId64
        ",\"input_id\":%" PRIu64 ",\"scenario\":\"%s\"}", seq, now, id, scenario_name(scenario));
}

folio_metrics_span_t folio_metrics_begin(void) {
    folio_metrics_span_t span;
    portENTER_CRITICAL(&s_lock);
    span.input_id = s_input_id;
    span.scenario = s_scenario;
    span.begin_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_lock);
    return span;
}

void folio_metrics_render_end(folio_metrics_span_t span) {
    const int64_t end = esp_timer_get_time();
    const size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "{\"v\":1,\"seq\":%" PRIu64 ",\"kind\":\"render\",\"begin_us\":%" PRId64
        ",\"end_us\":%" PRId64 ",\"duration_us\":%" PRId64 ",\"input_id\":%" PRIu64
        ",\"scenario\":\"%s\",\"internal_free\":%zu,\"psram_free\":%zu}",
        next_sequence(), span.begin_us, end, end - span.begin_us, span.input_id,
        scenario_name(span.scenario), internal, psram);
}

void folio_metrics_display_end(folio_metrics_span_t span, uint32_t mode,
    folio_metrics_wave_t wave, int x, int y, int w, int h, int result) {
    const int64_t end = esp_timer_get_time();
    const size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "{\"v\":1,\"seq\":%" PRIu64 ",\"kind\":\"display\",\"begin_us\":%" PRId64
        ",\"end_us\":%" PRId64 ",\"duration_us\":%" PRId64 ",\"input_id\":%" PRIu64
        ",\"scenario\":\"%s\",\"mode\":%" PRIu32 ",\"wave\":\"%s\",\"area\":[%d,%d,%d,%d],\"result\":%d"
        ",\"internal_free\":%zu,\"psram_free\":%zu}", next_sequence(), span.begin_us, end,
        end - span.begin_us, span.input_id, scenario_name(span.scenario), mode, wave_name(wave),
        x, y, w, h, result, internal, psram);
}
#endif
