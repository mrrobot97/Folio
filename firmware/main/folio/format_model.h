/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 排版值与会话纯模型，包含根草稿、子页候选和完整返回上下文。
 * Pure format values/session model with root draft, field candidate and return context.
 * 冻结：字段保留仅合入根草稿；只有服务持久 ACK 后生效。
 * Frozen: keeping a field only merges the root draft; activation requires a durable service ACK.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FOLIO_FORMAT_FONT_PATH_MAX 160
#define FOLIO_FORMAT_BOOK_PATH_MAX 288
#define FOLIO_FORMAT_STACK_MAX 8
typedef enum { FORMAT_BOOK = 1, FORMAT_DEFAULTS = 2, FOLIO_FORMAT_BOOK = FORMAT_BOOK, FOLIO_FORMAT_DEFAULTS = FORMAT_DEFAULTS } folio_format_scope_t;
typedef enum { FORMAT_FIELD_NONE, FORMAT_FIELD_FONT, FORMAT_FIELD_SIZE_WEIGHT, FORMAT_FIELD_SPACING, FORMAT_FIELD_BACKGROUND } folio_format_field_t;
typedef enum { FORMAT_REGULAR, FORMAT_MEDIUM, FORMAT_BOLD } folio_format_weight_t;
typedef enum { FORMAT_COMPACT, FORMAT_STANDARD, FORMAT_LOOSE } folio_format_spacing_t;
typedef enum { FORMAT_MARGIN_NARROW, FORMAT_MARGIN_STANDARD, FORMAT_MARGIN_WIDE } folio_format_margin_t;
typedef enum { FORMAT_PAPER_WHITE, FORMAT_PAPER_GRAY, FORMAT_PAPER_BLACK } folio_format_background_t;
typedef enum { FORMAT_CLOSED, FORMAT_EDITING, FORMAT_APPLIED, FORMAT_DISCARDED } folio_format_lifecycle_t;
typedef enum { FORMAT_ROUTE_READER=1, FORMAT_ROUTE_SETTINGS, FORMAT_ROUTE_ROOT, FORMAT_ROUTE_FONT, FORMAT_ROUTE_SIZE, FORMAT_ROUTE_SPACING, FORMAT_ROUTE_BACKGROUND, FORMAT_ROUTE_FONT_TRANSFER } folio_format_route_t;

typedef struct {
    char path[FOLIO_FORMAT_FONT_PATH_MAX];
    uint64_t file_size;
    int64_t mtime;
    uint32_t header_crc;
} folio_format_font_t;
typedef struct {
    folio_format_font_t font;
    uint8_t px, weight, spacing, margin, indent_em, background;
    /// M1 原生像素高度；非0时保留旧视觉字号，明确字号增减才切换新em。
    /// M1 native pixel height; nonzero retains old visual sizing until explicit size adjustment selects new em.
    uint8_t legacy_native_px;
    bool header_visible, footer_visible;
} folio_format_values_t;
typedef struct {
    uint64_t session_id, revision, active_revision, generation, suspension_id, transaction_id;
    folio_format_scope_t scope;
    folio_format_lifecycle_t lifecycle;
    char book_path[FOLIO_FORMAT_BOOK_PATH_MAX], book_title[128];
    uint64_t book_size;
    int64_t book_mtime;
    folio_format_values_t active, root, field_entry, candidate;
    folio_format_field_t field;
    folio_format_route_t editor_origin, field_parent, route, requested_leave;
    uint8_t selection, cursor, scroll, return_depth;
    uint8_t return_stack[FOLIO_FORMAT_STACK_MAX];
    bool font_child, font_transfer_stopped, root_dirty, field_dirty;
    /// 恢复提示由运行时设置，不代表最新 RAM 已保存。/ Runtime restore flags never imply all prior RAM was saved.
    bool recovered_checkpoint, resource_missing, commit_unknown;
    bool transient_baseline, edited, intent_attempted;
    folio_format_font_t font_resource;
} folio_format_snapshot_t;

folio_format_values_t folio_format_builtin_defaults(void);
bool folio_format_values_valid(const folio_format_values_t* values);
bool folio_format_values_equal(const folio_format_values_t* a, const folio_format_values_t* b);
int folio_format_weight_number(const folio_format_values_t* values);
int folio_format_line_height(const folio_format_values_t* values, int px);
int folio_format_margin_pixels(const folio_format_values_t* values);
uint8_t folio_format_paper(const folio_format_values_t* values);
uint8_t folio_format_ink(const folio_format_values_t* values);
bool folio_format_model_begin(folio_format_snapshot_t* snapshot);
bool folio_format_model_field_begin(folio_format_snapshot_t* snapshot,folio_format_field_t field);
bool folio_format_model_candidate(folio_format_snapshot_t* snapshot,const folio_format_values_t* values);
bool folio_format_model_field_keep(folio_format_snapshot_t* snapshot);
bool folio_format_model_field_cancel(folio_format_snapshot_t* snapshot);
