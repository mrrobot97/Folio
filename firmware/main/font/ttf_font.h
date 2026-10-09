/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 可变 TTF 字形缓存：卡上字体或内置子集，按字重光栅化后画到 framebuffer。
 *
 * Variable TTF glyph cache: SD fonts or the built-in subset, rasterized
 * at the current weight onto the framebuffer.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "epdiy.h"
#include "esp_err.h"

#define TTF_SIZE_SMALL 0
#define TTF_SIZE_LARGE 1

#define TTF_FONT_MAX 24
#define TTF_FONT_NAME_MAX 64
#define TTF_FONT_PATH_MAX 160

typedef struct {
    char name[TTF_FONT_NAME_MAX];
    char path[TTF_FONT_PATH_MAX];
} ttf_font_item_t;

typedef struct {
    uint32_t glyphs;
    uint32_t hits;
    uint32_t misses;
    int64_t read_us;
    int64_t raster_us;
    int64_t total_us;
} ttf_bench_stats_t;

#define TTF_FONT_BUILTIN "builtin"

/// UI 固定内置字体；正文和未应用候选彼此隔离。/ Fixed built-in UI; isolated active and candidate book faces.
typedef enum { TTF_ROLE_UI, TTF_ROLE_READER, TTF_ROLE_PREVIEW, TTF_ROLE_COUNT } ttf_font_role_t;
typedef struct {
    ttf_font_role_t previous;
    bool locked;
} ttf_font_scope_t;
typedef struct {
    char path[TTF_FONT_PATH_MAX];
    uint64_t file_size;
    int64_t mtime;
    uint32_t header_crc;
} ttf_font_identity_t;
typedef struct {
    size_t glyph_bytes;
    size_t glyph_limit;
    size_t resident_bytes;
    size_t stream_bytes;
    size_t mapped_bytes;
    size_t scratch_bytes;
    uint32_t glyph_entries;
    uint32_t hits;
    uint32_t misses;
    uint32_t evictions;
    uint32_t opens;
    bool ready;
    bool resource_failed;
} ttf_font_cache_stats_t;

/// 持有递归字体锁直到 end；只选已加载角色，不做文件 I/O。必须同任务严格嵌套。
/// Hold the recursive font lock until end; no file I/O. Pair in LIFO order on the same task.
esp_err_t ttf_font_begin(ttf_font_role_t role, ttf_font_scope_t* scope);
void ttf_font_end(ttf_font_scope_t* scope);
/// 事件阶段准备字体；失败仅影响目标角色。UI 仅接受 builtin。渲染阶段禁止调用。
/// Prepare during events; failure affects only the target. UI accepts only builtin. Never call from render.
esp_err_t ttf_font_prepare(ttf_font_role_t role, const char* path);
/// 交换候选与正文，旧正文留在候选供失败回滚；成功后 discard。不写设置或重新开文件。
/// Swap candidate/reader, retaining the old reader for rollback; discard after success. No settings/file opens.
esp_err_t ttf_font_apply_preview(void);
void ttf_font_discard_preview(void);
/// 拔卡后关闭所有外部字体，保留 UI；不会悄悄替正文选择其他字体。
/// Close external faces after media loss, retaining UI; never silently substitute a body font.
void ttf_font_media_lost(void);
/// 验证身份必须在事件/恢复阶段；CRC 是 SFNT 目录身份，不是安全签名或完整内容校验。
/// Validate during events/recovery; CRC identifies the SFNT directory, not a signature/full-content hash.
esp_err_t ttf_font_identity_read(const char* path, ttf_font_identity_t* out);
bool ttf_font_identity_equal(const ttf_font_identity_t* a, const ttf_font_identity_t* b);
bool ttf_font_has_codepoint(uint32_t codepoint);
/// 缺字返回 false 并给出首个 Unicode；不把 .notdef 当作支持。/ Report first missing Unicode instead of claiming .notdef support.
bool ttf_font_check_text(const char* text, uint32_t* first_missing);
bool ttf_font_resource_failed(void);
void ttf_font_cache_stats(ttf_font_role_t role, ttf_font_cache_stats_t* out);
/// 当前字体 em 到原引擎像素高，UI/正文预览共用；返回 0 表示无效。/ Convert current-face em to engine pixel height; zero if unavailable.
int ttf_font_pixel_height_for_em(int em);
/// 返回真实支持的字重范围；非可变字体返回 false 且固定为 400。/ Actual supported weight range; static faces return false and 400.
bool ttf_font_weight_range(int* minimum, int* maximum);

/// 灰阶字覆盖率伽马。小于 1 抬中间覆盖率，抗锯齿边缘更深；满墨仍是 0。
/// Coverage gamma for gray glyphs. Below 1 lifts mid coverage so AA edges are darker; full ink stays 0.
#ifndef TTF_COVER_GAMMA
#define TTF_COVER_GAMMA 0.6f
#endif

esp_err_t ttf_font_init(void);
esp_err_t ttf_font_open(const char* path);
esp_err_t ttf_font_open_builtin(void);
/// 传字体时关闭外部字体并暂停 SD 打开；UI 保留内置，解除后由上层显式恢复。
/// Close external faces and suspend SD opens for transfer; retain built-in UI and restore explicitly after resume.
esp_err_t ttf_font_suspend_sd(bool suspend);
bool ttf_font_is_builtin(void);
bool ttf_font_path_is_builtin(const char* path);
void ttf_font_unload(void);
int ttf_font_scan(void);
int ttf_font_count(void);
const ttf_font_item_t* ttf_font_item(int index);
const char* ttf_font_path(void);
const char* ttf_font_display_name(void);
bool ttf_font_ready(void);
int ttf_ascender(int size);
int ttf_ascender_px(int pixel_height);

void ttf_draw_text(
    uint8_t* framebuffer, int x, int y, int size, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);

void ttf_draw_text_px(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);
/// 覆盖率过半才落墨，像素只有 fg/bg。/ Ink only when coverage is over half; pixels are fg/bg only.
void ttf_draw_text_px_bw(
    uint8_t* framebuffer, int x, int y, int pixel_height, const char* text,
    enum EpdFontFlags align, uint8_t fg, uint8_t bg
);

void ttf_measure_line(int size, const char* text, int* above, int* below);
void ttf_measure_line_px(int pixel_height, const char* text, int* above, int* below);
int ttf_text_width_px(int pixel_height, const char* text);
void ttf_set_weight(int wght);
int ttf_get_weight(void);

void ttf_font_cache_clear(void);
void ttf_bench_begin(void);
void ttf_bench_end(ttf_bench_stats_t* out);
