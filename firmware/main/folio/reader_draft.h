/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 排版草稿状态和独立检查点，不处理正文重排。
 * Layout draft state and independent checkpoints, without pagination.
 * 冻结：编辑/保存草稿不等于应用；调用者成功重排后才提交应用值。
 * Frozen: editing/saving a draft does not apply it; commit only after successful reflow.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "reading_service.h"

typedef enum { FOLIO_FORMAT_BOOK = 1, FOLIO_FORMAT_DEFAULTS = 2 } folio_format_scope_t;
typedef enum { FOLIO_RESUME_READER = 1, FOLIO_RESUME_EDITOR = 2 } folio_format_origin_t;
#define FOLIO_FORMAT_FIELD_SIZE 1U
#define FOLIO_FORMAT_RETURN_DEPTH 8
#define FOLIO_FORMAT_MAX_BOOKS 128

typedef struct {
    char path[FOLIO_READING_PATH_MAX];
    uint32_t file_size;
    int applied_px, draft_px;
    bool editing, dirty;
    /// 草稿会话可暂存在后台；此字段区分工具条与正文来源。/ A session may remain in background; distinguish toolbar versus reading origin.
    bool editor_visible;
    /// DEFAULTS 保留枚举但本版未开放，保存/恢复会拒绝。/ DEFAULTS is reserved but unsupported; save/restore reject it.
    folio_format_scope_t scope;
    uint32_t supported_fields, pending_fields;
    int field_entry_px, pending_px;
    uint64_t session_id, revision, base_revision, suspension_id, confirmed_generation;
    uint8_t panel, selection, return_depth, return_stack[FOLIO_FORMAT_RETURN_DEPTH], font_child;
    folio_format_origin_t resume_origin;
    /// 本会话只编辑字号；字体为资源基线，不可在恢复时暗中替换。/ Size-only session; font is a resource baseline, never silently substitute it on restore.
    char font_path[192];
    uint32_t font_file_size;
    uint64_t book_mtime;
} folio_reader_draft_t;

/// 每本书独立封装已应用值和未应用草稿；关闭草稿与偏好提交共享一次 ACK。
/// Per-book envelope joins active values and unapplied draft; closure and preference commit share one ACK.
bool folio_reader_draft_load(const char* directory, const char* path, folio_reader_draft_t* out);
bool folio_reader_draft_save(const char* directory, const folio_reader_draft_t* draft);
void folio_reader_draft_begin(folio_reader_draft_t* draft, const char* path, uint32_t size, int applied_px);
bool folio_reader_draft_adjust(folio_reader_draft_t* draft, int direction);
void folio_reader_draft_applied(folio_reader_draft_t* draft);
void folio_reader_draft_cancel(folio_reader_draft_t* draft);
/// 两个匹配 ACK：按书会话包和本次休眠来源引用。失败不得断电。
/// Two matching ACKs: per-book session envelope and this suspension's origin reference. Failure forbids power-off.
bool folio_reader_draft_suspend(const char* directory, folio_reader_draft_t* draft, folio_format_origin_t origin);
/// 只接受仍未关闭、同session/suspension/generation的引用；资源验证由设备适配层处理。
/// Accept only unclosed matching session/suspension/generation; device adapter validates resources.
bool folio_reader_draft_resume(const char* directory, folio_reader_draft_t* out);
/// 无关联草稿的休眠来源写入持久屏障，防止复活历史会话。/ Persist a barrier for unrelated sleep origins, preventing historical-session resurrection.
bool folio_reader_draft_clear_resume(const char* directory);
/// 活跃 RAM 会话优先，不允许旧盘上快照覆盖；不支持的 RAM 状态拒绝回退。
/// Prefer the live RAM session over disk; unsupported RAM state must not fall back to an older checkpoint.
bool folio_reader_draft_recovery_candidate(const char* directory, const folio_reader_draft_t* live, folio_reader_draft_t* out);
typedef enum {
    FOLIO_DRAFT_SLEEP_CLEAR_ORIGIN,
    FOLIO_DRAFT_SLEEP_CHECKPOINT,
    FOLIO_DRAFT_SLEEP_BLOCK,
} folio_draft_sleep_action_t;
/// 任何未关闭会话都必须匹配当前可恢复上下文；离页不等于无草稿。
/// Every unclosed session requires a matching restorable context; leaving its page never means no draft.
folio_draft_sleep_action_t folio_reader_draft_sleep_policy(bool has_session, bool owner_active, bool context_supported);
/// 换书前实际写入未关闭会话；失败不得替换唯一 RAM 草稿，同一身份无需落盘。
/// Persist an unclosed session before replacing its book; failure forbids RAM replacement, same identity needs no write.
bool folio_reader_draft_preserve_before_open(const char* directory, const folio_reader_draft_t* live, const char* next_path, uint32_t next_size);
