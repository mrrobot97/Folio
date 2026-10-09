/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 小型双槽 CRC 文件检查点，保留上个有效槽。
 * Small dual-slot CRC file checkpoints, preserving the previous valid slot.
 * 冻结：不格式化、不擦除两个槽、不自动覆盖未知版本。
 * Frozen: never format, erase both slots, or overwrite unknown versions.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum {
    FOLIO_CHECKPOINT_OK = 0,
    FOLIO_CHECKPOINT_MISSING,
    FOLIO_CHECKPOINT_CORRUPT,
    FOLIO_CHECKPOINT_IO,
    FOLIO_CHECKPOINT_SCHEMA,
    FOLIO_CHECKPOINT_SIZE,
} folio_checkpoint_result_t;

/// stem 仅支持字母、数字、下划线；输出仅在成功时有效。/ Stem accepts letters, digits, underscore; outputs valid only on success.
folio_checkpoint_result_t folio_checkpoint_load(const char* directory, const char* stem,
    uint32_t schema, void* data, size_t cap, size_t* len, uint64_t* generation);
/// 写非当前槽并 flush/fsync/close/校验；失败不改调用方 generation。/ Write inactive slot, flush/fsync/close/verify; preserve generation on failure.
folio_checkpoint_result_t folio_checkpoint_save(const char* directory, const char* stem,
    uint32_t schema, const void* data, size_t len, uint64_t* generation);
