/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 单调时钟统计与有界、明确超限的离线历史。
 * Monotonic statistics and bounded offline history with explicit overflow.
 * 冻结：未知日期不会伪造为今天；检查点与进度独立；无自动数据清除。
 * Frozen: unknown dates never become today; checkpoints are independent of progress; never erase data automatically.
 */
#include "reading_service.h"
#include "checkpoint_store.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#define DAY_MS INT64_C(86400000)
#define BOOK_BYTES (FOLIO_READING_PATH_MAX + 128 + 8)
#define BASE_BYTES 40
#define WIRE_CAP (BASE_BYTES + FOLIO_READING_MAX_BOOKS * BOOK_BYTES + FOLIO_READING_MAX_DAYS * 14)
typedef struct { uint16_t book; int32_t day; uint64_t ms; } day_t;
typedef struct {
    folio_reading_book_t books[FOLIO_READING_MAX_BOOKS];
    day_t days[FOLIO_READING_MAX_DAYS];
    uint64_t total, unknown, unallocated;
    uint16_t book_count, day_count;
    bool full;
} history_t;
static history_t* s_history;
static char s_directory[320];
static int s_book = -1;
static bool s_has_book, s_visible, s_dirty, s_storage_ok, s_blocked, s_baseline_loaded;
static int64_t s_last_ms, s_activity_ms, s_clock_mono, s_clock_local_ms;
static bool s_clock_valid;
static uint32_t s_idle_ms = 300000;
static uint64_t s_generation;

static void* allocate(size_t size) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, size);
#endif
}
static void put(unsigned char** at, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) *(*at)++ = (unsigned char)(value >> (i * 8));
}
static uint64_t get(const unsigned char** at, unsigned bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= (uint64_t)*(*at)++ << (i * 8);
    return value;
}
static int32_t calendar_day(int64_t days) {
    // 公历换算不依赖宿主 TZ；时区已在可信时钟锚点中应用。
    // Gregorian conversion does not depend on host TZ; timezone is applied at the trusted anchor.
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int year = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned day = doy - (153 * mp + 2) / 5 + 1;
    int month = (int)mp + (mp < 10 ? 3 : -9);
    year += month <= 2;
    return year * 10000 + month * 100 + (int)day;
}
static void add_bucket(int32_t day, uint64_t ms) {
    s_history->total += ms;
    if (!day) s_history->unknown += ms;
    if (s_book >= 0) s_history->books[s_book].total_ms += ms;
    for (unsigned i = 0; i < s_history->day_count; ++i) {
        day_t* entry = &s_history->days[i];
        if (entry->book == (uint16_t)s_book && entry->day == day) { entry->ms += ms; s_dirty = true; return; }
    }
    if (s_book < 0 || s_history->day_count == FOLIO_READING_MAX_DAYS) {
        s_history->unallocated += ms;
        s_history->full = true;
    } else s_history->days[s_history->day_count++] = (day_t){(uint16_t)s_book, day, ms};
    s_dirty = true;
}
static void settle(int64_t now_ms) {
    if (!s_history || now_ms < s_last_ms) return;
    int64_t until = now_ms;
    int64_t deadline = s_activity_ms > INT64_MAX - s_idle_ms ? INT64_MAX : s_activity_ms + s_idle_ms;
    if (until > deadline) until = deadline;
    if (s_visible && s_has_book && until > s_last_ms) {
        int64_t start = s_last_ms;
        while (start < until) {
            if (!s_clock_valid) { add_bucket(0, (uint64_t)(until - start)); break; }
            int64_t local = s_clock_local_ms + (start - s_clock_mono);
            int64_t day = local / DAY_MS;
            int64_t left = DAY_MS - local % DAY_MS;
            int64_t delta = until - start < left ? until - start : left;
            add_bucket(calendar_day(day), (uint64_t)delta);
            start += delta;
        }
    }
    s_last_ms = now_ms;
}
static bool decode(const unsigned char* data, size_t len, history_t* target) {
    if (len < BASE_BYTES) return false;
    const unsigned char* p = data;
    uint16_t books = (uint16_t)get(&p, 2), days = (uint16_t)get(&p, 2);
    uint32_t full = (uint32_t)get(&p, 4);
    uint64_t total = get(&p, 8), unknown = get(&p, 8), unallocated = get(&p, 8);
    uint64_t reserved = get(&p, 8);
    if (books > FOLIO_READING_MAX_BOOKS || days > FOLIO_READING_MAX_DAYS || full > 1 || reserved ||
        len != BASE_BYTES + (size_t)books * BOOK_BYTES + (size_t)days * 14 || unknown > total || unallocated > total) return false;
    target->book_count = books; target->day_count = days;
    target->total = total; target->unknown = unknown; target->unallocated = unallocated; target->full = full;
    for (unsigned i = 0; i < books; ++i) {
        folio_reading_book_t* book = &target->books[i];
        memcpy(book->path, p, sizeof(book->path)); p += sizeof(book->path);
        memcpy(book->title, p, sizeof(book->title)); p += sizeof(book->title);
        book->total_ms = get(&p, 8);
        if (!memchr(book->path, 0, sizeof(book->path)) || !book->path[0] ||
            !memchr(book->title, 0, sizeof(book->title)) || book->total_ms > total) return false;
        for (unsigned j = 0; j < i; ++j) if (!strcmp(book->path, target->books[j].path)) return false;
    }
    for (unsigned i = 0; i < days; ++i) {
        day_t* day = &target->days[i];
        day->book = (uint16_t)get(&p, 2); day->day = (int32_t)get(&p, 4); day->ms = get(&p, 8);
        if (day->book >= books || day->ms > target->books[day->book].total_ms ||
            (day->day && (day->day < 20000101 || day->day > 22001231))) return false;
    }
    return true;
}
bool folio_reading_init(const char* directory) {
    if (s_history) return s_baseline_loaded ? s_storage_ok : folio_reading_retry_storage(directory);
    s_history = allocate(sizeof(*s_history));
    if (!s_history) return false;
    s_blocked = true;
    if (!directory || strlen(directory) >= sizeof(s_directory)) return false;
    memcpy(s_directory, directory, strlen(directory) + 1);
    if (mkdir(directory, 0700) && errno != EEXIST) return false;
    unsigned char* data = allocate(WIRE_CAP);
    if (!data) return false;
    size_t len = 0;
    folio_checkpoint_result_t result = folio_checkpoint_load(directory, "reading", 1, data, WIRE_CAP, &len, &s_generation);
    bool ok = result == FOLIO_CHECKPOINT_MISSING || (result == FOLIO_CHECKPOINT_OK && decode(data, len, s_history));
    free(data);
    if (!ok) memset(s_history, 0, sizeof(*s_history));
    s_storage_ok = ok; s_blocked = !ok; s_baseline_loaded = ok;
    return ok;
}
static bool add_checked(uint64_t* target, uint64_t value) {
    if (UINT64_MAX - *target < value) return false;
    *target += value; return true;
}
static bool merge_history(history_t* target, const history_t* delta, int map[FOLIO_READING_MAX_BOOKS]) {
    if (!add_checked(&target->total, delta->total) || !add_checked(&target->unknown, delta->unknown) ||
        !add_checked(&target->unallocated, delta->unallocated)) return false;
    target->full |= delta->full;
    for (unsigned i = 0; i < delta->book_count; ++i) {
        map[i] = -1;
        for (unsigned j = 0; j < target->book_count; ++j)
            if (!strcmp(delta->books[i].path, target->books[j].path)) { map[i] = (int)j; break; }
        if (map[i] < 0 && target->book_count < FOLIO_READING_MAX_BOOKS) {
            map[i] = target->book_count++;
            target->books[map[i]] = delta->books[i];
            target->books[map[i]].total_ms = 0;
        }
        if (map[i] >= 0) {
            if (!add_checked(&target->books[map[i]].total_ms, delta->books[i].total_ms)) return false;
        } else target->full = true;
    }
    for (unsigned i = 0; i < delta->day_count; ++i) {
        const day_t* source = &delta->days[i];
        int mapped = map[source->book], found = -1;
        if (mapped >= 0) for (unsigned j = 0; j < target->day_count; ++j)
            if (target->days[j].book == mapped && target->days[j].day == source->day) { found = (int)j; break; }
        if (found >= 0) {
            if (!add_checked(&target->days[found].ms, source->ms)) return false;
        } else if (mapped >= 0 && target->day_count < FOLIO_READING_MAX_DAYS) {
            target->days[target->day_count++] = (day_t){(uint16_t)mapped, source->day, source->ms};
        } else {
            if (!add_checked(&target->unallocated, source->ms)) return false;
            target->full = true;
        }
    }
    return true;
}
bool folio_reading_retry_storage(const char* directory) {
    if (!s_history) return folio_reading_init(directory);
    if (!directory || strcmp(directory, s_directory)) return false;
    if (mkdir(directory, 0700) && errno != EEXIST) return false;
    // 已合并的 RAM 含磁盘基线，写失败后再次重试不能重复叠加。
    // Once RAM contains the baseline, retries must never add the same disk history again.
    if (s_baseline_loaded) return folio_reading_checkpoint();
    unsigned char* data = allocate(WIRE_CAP);
    history_t* merged = allocate(sizeof(*merged));
    if (!data || !merged) { free(data); free(merged); return false; }
    size_t len = 0; uint64_t generation = 0;
    folio_checkpoint_result_t result = folio_checkpoint_load(directory, "reading", 1, data, WIRE_CAP, &len, &generation);
    bool valid_disk = result == FOLIO_CHECKPOINT_MISSING || (result == FOLIO_CHECKPOINT_OK && decode(data, len, merged));
    free(data);
    int map[FOLIO_READING_MAX_BOOKS];
    if (!valid_disk || !merge_history(merged, s_history, map)) { free(merged); return false; }
    if (s_book >= 0) s_book = map[s_book];
    free(s_history); s_history = merged;
    s_generation = generation; s_baseline_loaded = true; s_blocked = false; s_storage_ok = true;
    return folio_reading_checkpoint();
}
void folio_reading_set_idle_ms(uint32_t idle_ms) {
    s_idle_ms = idle_ms < 1000 ? 1000 : idle_ms > DAY_MS ? (uint32_t)DAY_MS : idle_ms;
}
void folio_reading_set_clock(int64_t unix_s, int32_t offset_minutes, int64_t mono_ms) {
    settle(mono_ms);
    s_clock_valid = unix_s >= INT64_C(946684800) && unix_s <= INT64_C(7289654399) &&
                    offset_minutes >= -840 && offset_minutes <= 840;
    s_clock_mono = mono_ms;
    if (s_clock_valid) s_clock_local_ms = (unix_s + (int64_t)offset_minutes * 60) * 1000;
}
bool folio_reading_open(const char* path, const char* title, int64_t mono_ms) {
    if (!s_history || !path || !path[0] || strlen(path) >= FOLIO_READING_PATH_MAX || mono_ms < s_last_ms) return false;
    settle(mono_ms);
    s_book = -1;
    for (unsigned i = 0; i < s_history->book_count; ++i) if (!strcmp(path, s_history->books[i].path)) { s_book = (int)i; break; }
    if (s_book < 0 && s_history->book_count < FOLIO_READING_MAX_BOOKS) {
        s_book = s_history->book_count++;
        folio_reading_book_t* book = &s_history->books[s_book];
        memcpy(book->path, path, strlen(path) + 1);
        if (!title || !title[0]) { title = strrchr(path, '/'); title = title ? title + 1 : path; }
        size_t count = strlen(title);
        if (count >= sizeof(book->title)) { count = sizeof(book->title) - 1; while (count && ((unsigned char)title[count] & 0xc0) == 0x80) --count; }
        memcpy(book->title, title, count); book->title[count] = 0;
        s_dirty = true;
    }
    s_has_book = s_visible = true;
    s_activity_ms = s_last_ms = mono_ms;
    return s_book >= 0;
}
void folio_reading_tick(int64_t mono_ms, bool reading_visible) {
    if (mono_ms < s_last_ms) return;
    settle(mono_ms);
    if (reading_visible && !s_visible) s_activity_ms = mono_ms;
    s_visible = reading_visible;
}
void folio_reading_activity(int64_t mono_ms) {
    if (mono_ms < s_last_ms) return;
    settle(mono_ms); s_activity_ms = mono_ms;
}
void folio_reading_pause(int64_t mono_ms) { folio_reading_tick(mono_ms, false); }
bool folio_reading_checkpoint(void) {
    if (!s_history || s_blocked) return false;
    if (!s_dirty) return s_storage_ok;
    unsigned char* data = allocate(WIRE_CAP);
    if (!data) { s_storage_ok = false; return false; }
    unsigned char* p = data;
    put(&p, s_history->book_count, 2); put(&p, s_history->day_count, 2); put(&p, s_history->full, 4);
    put(&p, s_history->total, 8); put(&p, s_history->unknown, 8); put(&p, s_history->unallocated, 8); put(&p, 0, 8);
    for (unsigned i = 0; i < s_history->book_count; ++i) {
        folio_reading_book_t* book = &s_history->books[i];
        memcpy(p, book->path, sizeof(book->path)); p += sizeof(book->path);
        memcpy(p, book->title, sizeof(book->title)); p += sizeof(book->title);
        put(&p, book->total_ms, 8);
    }
    for (unsigned i = 0; i < s_history->day_count; ++i) {
        day_t* day = &s_history->days[i];
        put(&p, day->book, 2); put(&p, (uint32_t)day->day, 4); put(&p, day->ms, 8);
    }
    s_storage_ok = folio_checkpoint_save(s_directory, "reading", 1, data, (size_t)(p - data), &s_generation) == FOLIO_CHECKPOINT_OK;
    if (s_storage_ok) s_dirty = false;
    free(data);
    return s_storage_ok;
}
folio_reading_summary_t folio_reading_summary_at(int64_t mono_ms) {
    folio_reading_summary_t result = {0};
    if (mono_ms < s_last_ms) mono_ms = s_last_ms;
    result.today_day = s_clock_valid ? calendar_day((s_clock_local_ms + mono_ms - s_clock_mono) / DAY_MS) : 0;
    if (!s_history) return result;
    result.total_ms = s_history->total; result.unknown_ms = s_history->unknown; result.unallocated_ms = s_history->unallocated;
    result.book_count = s_history->book_count; result.dirty = s_dirty; result.storage_ok = s_storage_ok; result.capacity_full = s_history->full;
    result.today_ms = result.today_day ? folio_reading_day_ms(result.today_day) : 0;
    return result;
}
folio_reading_summary_t folio_reading_summary(void) { return folio_reading_summary_at(s_last_ms); }
uint64_t folio_reading_day_ms(int32_t day) {
    uint64_t ms = 0;
    if (s_history) for (unsigned i = 0; i < s_history->day_count; ++i) if (s_history->days[i].day == day) ms += s_history->days[i].ms;
    return ms;
}
bool folio_reading_book(size_t index, folio_reading_book_t* out) {
    if (!s_history || !out || index >= s_history->book_count) return false;
    *out = s_history->books[index]; return true;
}
uint64_t folio_reading_book_day_ms(size_t index, int32_t day) {
    if (!s_history || index >= s_history->book_count) return 0;
    for (unsigned i = 0; i < s_history->day_count; ++i)
        if (s_history->days[i].book == index && s_history->days[i].day == day) return s_history->days[i].ms;
    return 0;
}
void folio_reading_date_bounds(int32_t* first, int32_t* last) {
    int32_t min = 0, max = 0;
    if (s_history) for (unsigned i = 0; i < s_history->day_count; ++i) {
        int32_t day = s_history->days[i].day;
        if (day && (!min || day < min)) min = day;
        if (day > max) max = day;
    }
    if (first) *first = min;
    if (last) *last = max;
}
void folio_reading_deinit(void) {
    free(s_history); s_history = NULL;
    s_book = -1; s_has_book = s_visible = s_dirty = s_storage_ok = s_blocked = s_clock_valid = s_baseline_loaded = false;
    s_last_ms = s_activity_ms = s_clock_mono = s_clock_local_ms = 0;
    s_idle_ms = 300000; s_generation = 0; s_directory[0] = 0;
}
