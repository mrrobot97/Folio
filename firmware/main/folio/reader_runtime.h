/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 阅读页导航和睡眠集成，不执行任何电源操作。
 * Reader navigation and sleep integration, without power operations.
 * 冻结：ECO 检查点失败必须由调用者取消断电；FAST 保留 RAM 草稿。
 * Frozen: callers must abort ECO power-off on failed checkpoints; FAST retains RAM drafts.
 */
#pragma once
#include "app.h"
#include <stddef.h>

/// 两个入口明确区分书架与续读，不隐式恢复无效挂载。/ Separate shelf/resume intent, never resume invalid mounts.
void app_book_show_library(app_ctx_t* ctx);
/// 首页搜索在扫描结束后进入现有搜索页。/ Home search enters the existing search view after scanning.
void app_book_show_search(app_ctx_t* ctx);
void app_book_resume_last(app_ctx_t* ctx);
/// 首页图书条目使用明确路径，仍验证挂载与文件。/ Home items open an explicit path with mount/file checks.
void app_book_open_path(app_ctx_t* ctx, const char* path);
/// 仅事件回调读取，含 NVS/stat I/O；缺文件或无有效进度返回 false。/ Event-only NVS/stat query; false for missing file/invalid progress.
bool app_book_recent(char* title, size_t title_cap, char* path, size_t path_cap, unsigned* percent);
/// 在睡前调用；ECO=false 失败仍可 FAST 睡眠但返回 false 供提示。/ Call before sleep; failed FAST saves still allow retained-RAM sleep, returning false for disclosure.
bool folio_reader_prepare_sleep(bool eco);
typedef struct {
    /// 三个独立 ACK；任一个成功不能覆盖其他失败。/ Three independent ACKs; one success never hides another failure.
    bool progress_ok, statistics_ok, draft_ok;
} folio_reader_save_status_t;
/// 最近一次睡眠准备的分项结果，无 I/O。/ I/O-free component results of the latest sleep preparation.
folio_reader_save_status_t folio_reader_save_status(void);
/// 尚未关闭的排版会话；完整 ECO 来源恢复仍需外层 journal ACK。/ Unclosed format session; complete ECO origin recovery still requires the outer journal ACK.
bool folio_reader_has_unapplied_draft(void);
/// 用户明确点击“恢复未应用草稿”时使用；优先当前 RAM，不把冷启动伪装成正常唤醒。
/// Explicit user recovery action; prefer current RAM and never present a cold boot as a verified normal wake.
bool app_book_restore_suspended(app_ctx_t* ctx);
/// 仅读取是否有完整匹配的盘上来源记录，不改 RAM，也不保证媒体已就绪。
/// Read-only check for a fully matching disk reference; never mutate RAM or imply media readiness.
bool folio_reader_has_suspended_draft(void);
/// 睡前失败后返回当前 RAM 编辑会话；资源失败保留草稿，调用方展示 blocked_reason。
/// Return to the live RAM editor after failed sleep; preserve drafts on resource failure, caller displays blocked_reason.
bool folio_reader_return_to_editor(app_ctx_t* ctx);
/// 最近阻塞原因，空串表示无阻塞。/ Latest blocked reason; empty means none.
const char* folio_reader_blocked_reason(void);
/// 仅在用户确认并成功重建存储后调用，丢弃旧 RAM 历史而不回写。
/// Only after user-confirmed successful storage reinitialization; drop old RAM history without writing it back.
bool folio_reader_storage_reinitialized(void);
/// 普通重新挂载：保留 RAM 统计增量与草稿，合并盘上历史后重试保存。/ Normal remount: retain RAM increments/drafts, merge disk history, retry persistence.
bool folio_reader_retry_storage(void);
/// 正常唤醒或取消睡眠后调用，不把睡眠时间计为阅读。/ Call after wake/aborted sleep, never count sleep duration.
void folio_reader_wake(void);
/// 菜单打开暂停、关闭后允许页内 tick 重新判断；不可从 render 调用。/ Pause for menu, then allow page ticks to resume; never from render.
void folio_reader_set_visible(bool visible);
