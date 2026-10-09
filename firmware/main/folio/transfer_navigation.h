/* SPDX-License-Identifier: Apache-2.0
 * 字体编辑的临时传输子流程，网络本身仍由官方传书组件负责。
 * Temporary font-editing transfer child; the upstream transfer component owns networking.
 */
#pragma once
#include "app.h"
/// 调用前由排版模型保存关联会话和返回栈；此函数只排队进入传书页。
/// The format model records session/return lineage first; this only queues the transfer page.
void app_transfer_open_font_child(app_ctx_t* ctx, const app_desc_t* return_page);
bool app_transfer_is_font_child(void);
/// 统一电源流程已停服后，清除待启动动作并提交关联字体子流程的停止回执。
/// After the power flow stops service, clear queued starts and acknowledge the linked font child.
void app_transfer_power_stopped(void);
