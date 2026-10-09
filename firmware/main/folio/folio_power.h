/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "app.h"
/// 统一保存/睡眠流程；失败可重试或返回，永不自动丢弃草稿或改电源模式。
/// Shared checkpoint/sleep flow; retry or return on failure, never discard drafts or change mode automatically.
void folio_power_request(app_ctx_t* ctx, bool poweroff, int64_t* ignore_until_ms);
