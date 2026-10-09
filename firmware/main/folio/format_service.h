/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 排版编辑事件 API；快照纯查询，所有写入由 UI 线程串行调用。
 * Format-editor event API; snapshots are pure and all writes are UI-thread serialized.
 * 冻结：BOOK 与 DEFAULTS 分离，字段预览和根提交不混同。
 * Frozen: keep BOOK and DEFAULTS separate; field previews never imply root commits.
 */
#pragma once
#include "format_model.h"
#include "app.h"

/// 打开当前书/新书默认编辑器；失败留原会话并通过 error 报告。/ Open current-book/default editor; preserve existing sessions on failure and report error.
bool folio_format_begin_book(app_ctx_t* ctx);
bool folio_format_begin_defaults(app_ctx_t* ctx);
/// 返回稳定只读快照；render 可调用，禁止修改返回对象。/ Stable read-only snapshot, safe in render; never mutate it.
const folio_format_snapshot_t* folio_format_snapshot(void);
const char* folio_format_error(void);
bool folio_format_field_begin(folio_format_field_t field);
bool folio_format_size_delta(int delta);
bool folio_format_set_weight(folio_format_weight_t weight);
bool folio_format_set_spacing(folio_format_spacing_t spacing);
bool folio_format_set_margin(folio_format_margin_t margin);
bool folio_format_set_indent(unsigned em);
bool folio_format_set_background(folio_format_background_t background);
bool folio_format_set_font(const folio_format_font_t* font);
bool folio_format_field_keep(void);
bool folio_format_field_cancel(void);
/// 预备分页/字体后执行持久事务；成功后 BOOK 请求阅读页，DEFAULTS 返回来源。
/// Prepare pagination/font then persist; success requests the reader for BOOK or origin for DEFAULTS.
bool folio_format_apply(app_ctx_t* ctx);
/// 仅成功写入关闭屏障后退出；失败保留原根草稿与子页候选。/ Exit only after durable closure; preserve root/field candidates on failure.
bool folio_format_discard(app_ctx_t* ctx);
/// 保存选择/滚动与子流程来源，绝不触发 HTTP 上传。/ Capture selection/scroll and child lineage; never start HTTP.
bool folio_format_set_view(uint8_t selection, uint8_t cursor, uint8_t scroll);
bool folio_format_enter_font_child(bool transfer);
bool folio_format_leave_font_child(bool transfer_stopped);
/// 同一会话传输 STOP/join ACK 的幂等通知，TRANSFER→FONT；重复通知不弹栈。
/// Idempotent STOP/join ACK for this session, TRANSFER to FONT; repeated notifications never pop again.
bool folio_format_transfer_stopped(void);
bool folio_format_request_leave(folio_format_route_t destination);
void folio_format_cancel_leave(void);
/// 仅该会话已存在时恢复 UI；不会从磁盘覆盖更新 RAM。/ Restore existing UI session only; disk never overwrites newer RAM.
bool folio_format_return_to_editor(app_ctx_t* ctx);
bool folio_format_has_session(void);
/// 编辑器/字体页 on_enter/on_exit 调用；暂态电源覆盖不结束所有权。/ Editor/picker on_enter/on_exit only; power overlays do not end ownership.
void folio_format_set_owner_visible(bool visible);
/// UI 媒体失效回调：先停止 reader/preview 消费者，保留会话，再由主循环 invalidate 字体。
/// UI media-loss callback: stop reader/preview consumers while retaining the session; loop invalidates fonts afterward.
void folio_format_media_lost(app_ctx_t* ctx);
/// 电源请求首次捕获来源时调用一次，不能用停止传输后的页面倒推。/ Call once when power origin is captured, never infer it after stopping transfer.
void folio_format_capture_power_origin(const app_desc_t* origin);
bool folio_format_prepare_book_change(const char* next_path);
bool folio_format_restore_book_session(app_ctx_t* ctx,const char* path);
/// 仅冷启动/用户确认重建后清 RAM 状态，文件不删除。/ Clear RAM only on cold boot or confirmed reinitialization; never delete files.
void folio_format_reset_runtime(void);
bool folio_format_read_baseline_pending(const char* path);
bool folio_format_flush_read_baseline(void);
bool folio_format_recover_reader_font(app_ctx_t* ctx);
bool folio_format_open_book_recovery(app_ctx_t* ctx,const char* path);
/// 主睡眠入口调用；完整会话/来源未 ACK 时拒绝 ECO/OFF。/ Main sleep gate; reject ECO/OFF without full session/origin ACK.
bool folio_format_prepare_sleep(bool eco);
void folio_format_wake(void);
/// 启动只恢复最后匹配的已确认会话；未知/损坏保留原文件，无有效来源返回 NULL。
/// Boot restores only the last matching acknowledged session; preserve unknown/corrupt files, NULL without a valid origin.
const app_desc_t* folio_format_restore_boot(void);
/// 读已有按书值/新书默认；legacy仅用于无新格式记录的已读书，不改变已打开书。
/// Resolve existing book values/new-book defaults; legacy only seeds previously read books without a new record.
bool folio_format_resolve_book(const char* path,uint64_t size,int64_t mtime,bool previously_read,uint8_t progress_format,
    const folio_format_values_t* legacy,folio_format_values_t* out,uint64_t* revision);
