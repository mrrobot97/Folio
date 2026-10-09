# 继续开发入口：M2 收尾节点

本节点版本为 **0.2.0**。本节点结束后停止开发，等待用户决定下一阶段的范围与投入上限。**未完整还原 354 张画板，未做真机验证，也不是可公开发布的正式固件。** 最终构建、源码指纹和测试结果见同目录 `VALIDATION_M2.json`；具体页面映射见 [PRODUCT_UI.md](PRODUCT_UI.md)。

## 当前范围

| 模块 | 已实现 | 边界 |
| --- | --- | --- |
| 阅读 | 官方 TXT/EPUB、编码、目录、逐书进度；正文多字段排版、候选页表、中文禁则、页脚进度 | 书架/部分工具及诊断页仍沿用官方界面；复杂 EPUB 与长章节需真机 |
| 排版 | BOOK/DEFAULTS、字体/字号/字重/行距/边距/缩进/阅读背景；字段候选、根草稿、明确应用/放弃 | 页眉页脚/翻页方式的独立设置子页未完成；全局深色暂缓 |
| 字体 | UI/已应用正文/候选预览隔离；真实可变字重；资源身份、缺字提示与上传返回 | 内置子集不覆盖任意中文正文，需要完整 TF 字体；实际墨色/残影待测 |
| 保存恢复 | 独立进度、统计、草稿；双槽、意图记录、不确定写入协调、迁移、配额与空间预检 | FAT/Flash 真实断电与回滚兼容仍需验证；失败不擦分区、不淘汰历史 |
| 首页/统计 | 真实续读、搜索、按日/书统计、热力图、跨午夜处理 | 首页书签快捷项暂由统计替代；真实封面和可靠导入时间未完成 |
| 系统 | 手动校时、FAST/ECO、拿起唤醒、TF 壁纸、显式存储恢复 | 自动休眠未启用；低电量的非 OTA 行为、完整诊断/备份体验待完善 |
| OTA/网页 | 准入/完整性/置备策略、候选分区、打包工具、网页刷机校验与 v0.4.2 设计图库 | OTA 产品入口/启动健康确认未闭环，相关函数可能被链接裁剪；未启用 Pages/Release，未刷设备 |

进度 v3 在原有等长记录中标记旧 native 像素、M2 em、临时默认与迁移 native 来源，继续读取 v1/v2。临时默认重开按可恢复配置或当前默认处理，不再误当旧字号；已有 M2 em 进度但权威排版丢失时安全报错。降级到旧固件的兼容性必须单独验证，不能据此宣称 OTA 回滚数据兼容。

128 本排版、64 本统计身份和 4096 个书日桶是当前工程边界，不是用户认可的最终产品上限，也不保证 20 KiB NVS 能存 128 本进度。详见 [CAPACITY.md](CAPACITY.md)。纯默认、没有持久意图的新书可临时阅读；明确放弃 RAM 编辑有退路。真实草稿、已有记录及不确定写入不得借此静默丢弃。

## 设计和证据

稳定基线为 `read_pico_firmware/docs/local/ui-redesign/v0.4.2/output`；设计 ZIP SHA-256：`dc4fcfafd366840f406bbb968b26f6c35e9bcad2e0563f18123b02de9abc6758`。官方源码参考保持只读，上游提交为 `5f3406deec33304de8801046db583dec7c49e55b`，来源/许可见 [SOURCES.md](SOURCES.md)。

运行 UI、格式 UI 和 reader-device 测试会生成原尺寸 PNG 与源码/字体 manifest。基础 UI 套件含字体替身；格式 UI 使用生产字体引擎但注入服务结果；reader-device 联合生产服务、字体、分页与提取的 reader 回调。它们都不是屏幕照片，不证明真实触控、PMU、耗电或整机并发。

已做的可量化工作：日历三次提交合为一次；移除 FAST 进出额外刷白；三字体 glyph 缓存合计上限 1.5 MiB；分页行缓冲按行增长并限 4 KiB；事务临时区移入有界 PSRAM，消除已发现的 16 KiB 主栈超限链。可选观测默认关闭，支持输入/render/display 与堆快照；方法和限制见 [PERFORMANCE.md](PERFORMANCE.md)。当前没有真机延迟、电流或续航改善数据。

## 复跑环境与命令

- ESP-IDF **v6.1**，固定 `fff9895c82d744c7237be8847347bdd1b07c6643`；Xtensa 工具版本由其 `tools.json` 固定。
- FontTools **4.66.1**；网页 esptool-js **0.7.0**，npm 锁文件保留。工具在 `.tools`、缓存在 `.cache`，均不提交。
- 保留 Zbit 120 MHz Flash/PSRAM 默认参数、BSP、VCOM 和原波形，不用 `sdkconfig.ci` 冒充产品配置。

从仓库根运行：

```bash
bash scripts/build_firmware.sh
bash scripts/test_system.sh
sh scripts/test_reader.sh
sh scripts/test_reader_format.sh
sh scripts/test_reader_device.sh
bash scripts/test_fonts.sh
sh tests/power/run.sh
sh tests/ui/run.sh
sh tests/ui/run_format.sh
sh tests/metrics/run.sh
PYTHONPATH="$PWD/.tools/python-assets" python3 scripts/check_font.py
python3 scripts/storage_budget.py
python3 scripts/source_fingerprint.py
npm --prefix web run build
npm --prefix web test
```

新文案生成字体：`PYTHONPATH="$PWD/.tools/python-assets" python3 firmware/tools/gen_builtin_font.py`。不要手改字体或第三方生成表。

诊断配置单独构建，不改默认配置：

```bash
bash scripts/build_firmware.sh -B "$PWD/build/metrics" \
  -DSDKCONFIG="$PWD/build/metrics/sdkconfig" \
  '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.metrics'
```

网页本地预览：`npm --prefix web run preview`。Pages/Release 没有开启，网页无真实包/设备校验时不能写入，也不自动擦除整片 Flash。

## 后续按优先级执行

1. **限定真机验收**：确认板型、Flash、真实分区和数据备份；获得针对具体包/地址的刷写授权后，验证启动、基本读书、字体、恢复、拔卡、容量不足及受控断电。先采集少量固定场景，再决定优化；不用模拟值填真机报告。
2. **容量与维护体验**：测实际 FAT 簇/WL/NVS 可用条目，评估进度容量、排版配额、写入频率和已有记录的管理/导出方案；比较更紧凑存储方案后再改，不继续叠加日志或自动删旧数据。
3. **剩余产品功能**：书签与首页入口、封面/导入排序、自动休眠安全、系统/传书/书架视觉统一。每次只选一项，先写验收范围和投入上限。
4. **OTA 闭环**：接产品入口和启动健康确认，核实真实板型与已安装启动器的回滚能力；验证新旧数据格式兼容、低电量/断电/回滚/串口恢复后再发布。当前默认 factory 布局拒绝 OTA。
5. **独立资源优化**：已完成但未接线/未纳入本提交的压缩草案保留在本机工作区：`firmware/main/folio/embedded_image.*`、`firmware/main/assets/*_4bpp.fimg`、`scripts/pack_images.py`、`tests/assets/`、`docs/ASSETS.md`。单独验证接线后再提交；不能把压缩资源省下的字节当成当前固件已经减少的大小。

## 仍需用户决定或授权

- 闲置计时阈值、自动休眠时长、壁纸 TF/网页导入取舍；不把图稿示例当决定。
- 保存失败后的 C1/C2 自动降级没有启用；不得自动放弃草稿或改为另一电源模式。
- 真机连接/刷写、分区迁移、熔丝/密钥/签名操作，及公开 Release、Pages 启用均须明确确认；不会因本次 commit/push 自动执行。
- 下一轮的功能范围与投入上限。本文交付后不继续后台开发、监控或迭代。
