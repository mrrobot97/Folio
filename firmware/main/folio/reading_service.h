/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 离线阅读统计；所有调用由 UI 线程串行化，查询无 I/O。
 * Offline reading statistics; UI-thread serialized calls and I/O-free queries.
 * 冻结：未知日期单列；按单调时间计时；进度不存入统计检查点。
 * Frozen: retain unknown dates, use monotonic duration, never store reading progress here.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FOLIO_READING_MAX_BOOKS 64
#define FOLIO_READING_MAX_DAYS 4096
#define FOLIO_READING_PATH_MAX 288

typedef struct {
    /// 毫秒总计、未知日期、今日、容量超限未分类时间。/ Total, undated, today and capacity-overflow milliseconds.
    uint64_t total_ms, unknown_ms, today_ms, unallocated_ms;
    /// YYYYMMDD；日期未知时为 0。/ YYYYMMDD, zero when unknown.
    int32_t today_day;
    size_t book_count;
    /// 脏状态、存储可用、容量已满。/ Dirty, persistence available and full capacity.
    bool dirty, storage_ok, capacity_full;
} folio_reading_summary_t;

typedef struct {
    char path[FOLIO_READING_PATH_MAX];
    char title[128];
    uint64_t total_ms;
} folio_reading_book_t;

/// 载入双检查点；不存在可新建，损坏/未知版本只读保留。重复初始化不重置 RAM。
/// Load dual checkpoints; missing is new, corrupt/unknown schema is preserved. Reinit retains RAM.
bool folio_reading_init(const char* directory);
/// 暂时挂载失败后的无损恢复：未加载基线时合并磁盘历史与 RAM 新增量，仅合并一次。
/// Lossless retry after transient mount failure: merge disk history with RAM-only increments exactly once.
bool folio_reading_retry_storage(const char* directory);
/// 可配置闲置上限，默认 5 分钟，至少 1 秒。/ Configurable idle cap, default five minutes, minimum one second.
void folio_reading_set_idle_ms(uint32_t idle_ms);
/// 设置可信时间；unix_s < 0 退回未知日期。切换前先结算旧时间，不追溯重分配。
/// Set trusted time; negative unix_s clears it. Settle old time first, never reattribute history.
void folio_reading_set_clock(int64_t unix_s, int32_t utc_offset_minutes, int64_t mono_ms);
/// 打开书时开始计时；路径用作稳定 ID。过量图书仍统计总计并标为未分类。
/// Begin a book, identified by full path. Excess books retain totals as unallocated time.
bool folio_reading_open(const char* path, const char* title, int64_t mono_ms);
/// tick 切换阅读可见性；工具/目录/菜单/睡眠须传 false。/ Tick visibility; tools/TOC/menu/sleep must pass false.
void folio_reading_tick(int64_t mono_ms, bool reading_visible);
/// 用户动作重置闲置截止，不把截止后的空闲时间补入。/ Activity resets the idle deadline without backfilling idle time.
void folio_reading_activity(int64_t mono_ms);
/// 暂停且结算，不关闭图书身份。/ Settle and pause while retaining book identity.
void folio_reading_pause(int64_t mono_ms);
/// 仅脏时写入，成功时清脏；失败保留 RAM。/ Write only dirty state; clear dirty on success, retain RAM on failure.
bool folio_reading_checkpoint(void);
/// 生命周期结束，主要用于宿主测试。/ End lifecycle, primarily for host tests.
void folio_reading_deinit(void);
folio_reading_summary_t folio_reading_summary(void);
/// 当前时刻的只读快照：推进日期判定，不结算或增加阅读时长。/ Read-only snapshot at now: advance the date, never settle or accrue reading time.
folio_reading_summary_t folio_reading_summary_at(int64_t mono_ms);
uint64_t folio_reading_day_ms(int32_t day);
bool folio_reading_book(size_t index, folio_reading_book_t* out);
uint64_t folio_reading_book_day_ms(size_t index, int32_t day);
/// 已保存/内存中的真实日期范围；无已知日期输出 0，不推断今天。/ Actual dated-history bounds; zero when absent, never infer today.
void folio_reading_date_bounds(int32_t* first, int32_t* last);
