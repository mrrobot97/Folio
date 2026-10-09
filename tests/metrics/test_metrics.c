/* SPDX-License-Identifier: Apache-2.0 */
#include "folio_metrics.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static atomic_llong now_us;
static unsigned heap_reads;
static char last_line[1024];
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

int64_t esp_timer_get_time(void) {
    return atomic_fetch_add(&now_us, 1);
}

size_t heap_caps_get_free_size(uint32_t caps) {
    ++heap_reads;
    atomic_fetch_add(&now_us, 50);
    assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) || caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return caps & MALLOC_CAP_INTERNAL ? 100000 : 6000000;
}

void metrics_test_log(const char* tag, const char* format, ...) {
    pthread_mutex_lock(&log_lock);
    assert(strcmp(tag, "folio_metrics") == 0);
    va_list args;
    va_start(args, format);
    const int size = vsnprintf(last_line, sizeof(last_line), format, args);
    va_end(args);
    assert(size > 0 && (size_t)size < sizeof(last_line));
    printf("I (0) %s: %s\n", tag, last_line);
    atomic_fetch_add(&now_us, 7);
    pthread_mutex_unlock(&log_lock);
}

static void* snapshot_worker(void* context) {
    (void)context;
    for (int i = 0; i < 1000; ++i) {
        folio_metrics_span_t span = folio_metrics_begin();
        assert(span.scenario == FOLIO_METRICS_OTHER || span.scenario == FOLIO_METRICS_SYSTEM);
        assert(span.input_id == 0);
    }
    return NULL;
}

int main(void) {
    atomic_store(&now_us, 100);
    folio_metrics_init();
    atomic_store(&now_us, 1000);
    folio_metrics_input(FOLIO_METRICS_READER, FOLIO_METRICS_KEY);
    atomic_store(&now_us, 1100);
    folio_metrics_span_t render = folio_metrics_begin();
    assert(render.begin_us == 1100 && render.input_id == 2);
    atomic_store(&now_us, 1200);
    folio_metrics_render_end(render);
    assert(strstr(last_line, "\"duration_us\":100,") != NULL);
    assert(strstr(last_line, "\"end_us\":1200,") != NULL);
    assert(heap_reads == 2);
    atomic_store(&now_us, 1300);
    folio_metrics_span_t display = folio_metrics_begin();
    atomic_store(&now_us, 1800);
    folio_metrics_display_end(display, 5, FOLIO_METRICS_WAVE_PANEL, 12, 34, 50, 60, 0);
    assert(strstr(last_line, "\"area\":[12,34,50,60]") != NULL);
    assert(strstr(last_line, "\"duration_us\":500,") != NULL);
    atomic_store(&now_us, 1900);
    display = folio_metrics_begin();
    atomic_store(&now_us, 2500);
    folio_metrics_display_end(display, 0x12, FOLIO_METRICS_WAVE_PANEL, 0, 0, 684, 1216, 1);
    atomic_store(&now_us, 2600);
    folio_metrics_input_end();
    atomic_store(&now_us, 3000);
    folio_metrics_input(FOLIO_METRICS_STATS, FOLIO_METRICS_TOUCH);
    atomic_store(&now_us, 3100);
    folio_metrics_input_end();
    atomic_store(&now_us, 4000);
    folio_metrics_input(FOLIO_METRICS_HOME, FOLIO_METRICS_GESTURE);
    atomic_store(&now_us, 4100);
    render = folio_metrics_begin();
    atomic_store(&now_us, 4200);
    folio_metrics_input_end();
    atomic_store(&now_us, 4300);
    folio_metrics_input(FOLIO_METRICS_STATS, FOLIO_METRICS_POWER);
    atomic_store(&now_us, 4400);
    folio_metrics_render_end(render);
    assert(strstr(last_line, "\"scenario\":\"home\"") != NULL);
    atomic_store(&now_us, 4500);
    folio_metrics_input_end();
    folio_metrics_set_scenario(FOLIO_METRICS_SYSTEM);
    atomic_store(&now_us, 4600);
    render = folio_metrics_begin();
    atomic_store(&now_us, 4700);
    folio_metrics_render_end(render);
    assert(strstr(last_line, "\"input_id\":0,") != NULL);
    folio_metrics_input((folio_metrics_scenario_t)-1, (folio_metrics_input_kind_t)-1);
    assert(strstr(last_line, "\"scenario\":\"other\",\"input\":\"unknown\"") != NULL);
    folio_metrics_input_end();
    folio_metrics_input_end();
    folio_metrics_set_scenario(FOLIO_METRICS_OTHER);
    pthread_t worker;
    assert(pthread_create(&worker, NULL, snapshot_worker, NULL) == 0);
    for (int i = 0; i < 1000; ++i)
        folio_metrics_set_scenario(i % 2 ? FOLIO_METRICS_OTHER : FOLIO_METRICS_SYSTEM);
    assert(pthread_join(worker, NULL) == 0);
    fprintf(stderr, "metrics enabled: timing, correlation, concurrent snapshots and fixed labels PASS (host only)\n");
    return 0;
}
