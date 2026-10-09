/*
 * SPDX-FileCopyrightText: 2026 Folio contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：产品页的声明，页面不相互包含实现。
 * English: Product page declarations without cross-including implementations.
 */
#pragma once
#include "app.h"
extern const app_desc_t app_folio_home;
extern const app_desc_t app_folio_stats;
extern const app_desc_t app_folio_settings;
extern const app_desc_t app_folio_sleep;
extern const app_desc_t app_folio_storage;
/// 显式打开总览，统计子页返回保留其年月日。/ Open overview explicitly; subpage back retains its date.
void folio_stats_open(app_ctx_t *ctx);
