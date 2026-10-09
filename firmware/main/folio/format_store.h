/* SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 排版单包事务与来源journal；仅供服务调用。/ Format envelope transactions and origin journals, service-only.
 */
#pragma once
#include "format_model.h"
#include "checkpoint_store.h"
folio_checkpoint_result_t folio_format_store_load(const char* directory,folio_format_scope_t scope,const char* path,folio_format_snapshot_t* out);
bool folio_format_store_save(const char* directory,folio_format_snapshot_t* snapshot);
bool folio_format_store_suspend(const char* directory,folio_format_snapshot_t* snapshot);
folio_checkpoint_result_t folio_format_store_resume(const char* directory,folio_format_snapshot_t* out);
bool folio_format_store_clear_resume(const char* directory);
typedef enum { FORMAT_STORE_OK, FORMAT_STORE_FAILED, FORMAT_STORE_UNKNOWN, FORMAT_STORE_COMMITTED_NEEDS_FINALIZE, FORMAT_STORE_CAPACITY, FORMAT_STORE_NO_SPACE } folio_format_store_status_t;
typedef bool (*folio_format_space_query_t)(uint64_t* free_bytes,void* context);
/// NULL 使用设备 FAT 查询；宿主测试必须显式注入，未知容量拒绝写入。/ NULL uses device FAT; host tests inject explicitly, unknown capacity rejects writes.
void folio_format_store_set_space_query(folio_format_space_query_t query,void* context);
bool folio_format_store_require_space(uint64_t bytes);
#define FOLIO_FORMAT_TRANSACTION_RESERVE (32U*1024U)
#define FOLIO_FORMAT_BASELINE_RESERVE (8U*1024U)
#define FOLIO_FORMAT_REFERENCE_RESERVE (8U*1024U)
/// 首次已应用默认的稀疏基线，只允许无会话/无候选/无前记录，绝不用于 Apply。
/// Sparse initial baseline only without a session/candidate/predecessor; never use for Apply.
bool folio_format_store_seed_baseline(const char* directory,folio_format_snapshot_t* snapshot);
bool folio_format_store_transition(const char* directory,const folio_format_snapshot_t* live,folio_format_snapshot_t* target);
bool folio_format_store_finalize(const char* directory);
folio_format_store_status_t folio_format_store_status(void);
folio_checkpoint_result_t folio_format_store_recover_pending(const char* directory,folio_format_snapshot_t* out);
/// 启动/宿主重启仿真或用户确认重建后清运行态，绝不删除文件。/ Clear runtime state for boot/host cold-boot simulation or confirmed reinitialization; never delete files.
void folio_format_store_reset_runtime(void);

/// 内部原始编码/存储接口，只供事务层验证已知前驱，产品不得绕过公开 load。
/// Internal raw codec/storage for transaction predecessor validation; product code must not bypass public load.
#define FOLIO_FORMAT_WIRE_SIZE 2048
void folio_format_store_encode(const folio_format_snapshot_t* snapshot,unsigned char data[FOLIO_FORMAT_WIRE_SIZE]);
bool folio_format_store_decode(const unsigned char data[FOLIO_FORMAT_WIRE_SIZE],folio_format_snapshot_t* snapshot);
folio_checkpoint_result_t folio_format_store_raw_load(const char* directory,folio_format_scope_t scope,const char* path,folio_format_snapshot_t* out);
bool folio_format_store_raw_save(const char* directory,folio_format_snapshot_t* snapshot);
