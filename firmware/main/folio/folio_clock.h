/* SPDX-License-Identifier: Apache-2.0
 * 明确校时后的会话时钟；冷启动不把旧检查点当成当前时间。
 * Explicitly set session clock; never treat an old checkpoint as current time on cold boot.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/// 本地 UTC+8 日期；冷启动未知。/ UTC+8 local date, unknown after cold boot.
bool folio_clock_date(int* year, int* month, int* day);
/// 当前可信 Unix 秒；未知返回 0。/ Trusted Unix seconds; zero when unknown.
int64_t folio_clock_unix_seconds(void);
/// 显式手动校时，2020..2099，拒绝归一化错误日期。/ Explicit set, 2020..2099; reject invalid dates instead of normalizing.
bool folio_clock_set_local(int year, int month, int day, int hour, int minute);
bool folio_clock_local(int* year, int* month, int* day, int* hour, int* minute);
int folio_clock_month_days(int year, int month);
