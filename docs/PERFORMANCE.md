# 性能、省电与真机验收

当前证据分为生产代码变化、主机测量、固件编译和真机测量。主机绘制像素、C 函数耗时、调用次数均不能替代墨水屏延迟、残影、触摸体验或电流。

## 已有可复跑证据

| 项目 | 当前变化或测量 | 复跑 |
| --- | --- | --- |
| 日历选日 | 三个逻辑脏区合并为一次 GL16 提交；样例合并框 389,360 / 831,744 像素（46.81%），2,610 个变化像素全部包含（当前字体版本）。官方高层仍全屏扫描，不把矩形面积当扫描比例。 | `sh tests/ui/run.sh` |
| FAST 进出 | 删除进锁屏与唤醒前各一次额外 `epd_clear`，保留参考帧，最终锁屏只提交一次。保留官方波形和每 14 次软刷的清残影预算。 | `sh tests/power/run.sh`、`bash firmware/tools/run_display_host_test.sh` |
| 统计查询 | 固定上限 64 本 / 4096 书日桶，查数据不读文件；满桶单书检查点 57,836 字节，10,000 次全历史按日查询在 sanitizer 主机约 70 ms，必须以复跑输出为准。 | `sh scripts/test_reader.sh` |
| 字体 | 继续复用官方 PSRAM LRU、扇区缓存及字宽度量；产品 UI 做 em 与实际字宽换算。内置可变子集覆盖全部扫描的界面字符。 | `PYTHONPATH=.tools/python-assets python3 scripts/check_font.py`、`python3 firmware/tools/test_font_metrics.py` |
| WiFi 生命周期 | 睡眠前停止空闲传输；正在写文件时拒绝睡眠，最终断电前再次检查。保留官方停止 HTTP、等待请求结束、停 WiFi 的路径。 | `sh tests/power/run.sh`、官方传书回归 |
| 字体与分页内存 | UI/正文/预览 glyph 缓存上限分别为 128 KiB/1 MiB/384 KiB；行缓冲从 256 B 按需增长、上限 4 KiB。三角色字体生产引擎的主机分配峰值约 3.45 MB，不能当整机峰值。 | `bash scripts/test_fonts.sh`、官方分页回归 |
| 事务栈 | 事务临时记录与序列化缓冲使用有界 PSRAM 分配，避免先前多层大局部数组超过 16 KiB 主栈。仍需设备测量全路径栈水位。 | `sh scripts/test_reader_format.sh`、默认与 metrics 完整构建 |

## 明确尚未优化或尚未测量

官方主循环仍以 5 ms 周期轮询；当前没有开启动态电源管理，也没有降低 CPU/Flash/PSRAM 频率。官方阅读预排版同步等待仍需进一步故障和响应测试。BSP、波形、PSRAM 缓存和预渲染都是上游复用能力，不能当作本次新增成果。

目前没有设备连接、刷写或电流数据；不能给出耗电改善百分比、续航天数、真实翻页 p95 或“无残影”结论。

## 真机执行顺序

先核验 RDP-G01-W、Flash 型号/容量、现有分区、供电与当前固件，备份用户数据。只有用户确认的对应构建可以刷写，地址必须取该构建的 `flash_args`；不能套用别的包的地址，也不能自动擦除数据。先确认串口启动与输入响应，再执行以下相同素材/温度/电量的基线和 Folio 对比。

1. 保存固件 SHA-256、IDF 提交、sdkconfig、字体 SHA、素材 SHA、环境温度、电池百分比和供电方式。
2. 串口采集：`idf.py -C firmware -p <实际串口> monitor`。日志留本地，分享前去掉 UID/MAC/SSID。只有“能编译”不能替代真实启动确认。
3. 首页/书架/正文/工具条分别执行 30 次轻点、连续翻页和返回，记录输入→首次反馈及翻页 p50/p95，并拆分排版、等待预渲染、绘制和显示事务。使用同一套日志或外部高速录像定义时间点。
4. 日历选日/月切换分别记录每操作波形事务数、GC16 次数与实际耗时，观察灰底和旧字残影；不能仅凭少调用两次就宣称快了特定比例。
5. 使用大 TXT、复杂 EPUB、含图片/长标题 EPUB 和完整 TF 字体，记录内部堆/PSRAM 最低余量、打开/翻章耗时、失败页面及返回耗时。无卡、拔卡、损坏书和缺字体都应保留进度/草稿。
6. 统计写入记录每次检查点的字节数、耗时、触发原因；满容量和故障重试不得连续无限写盘。FAT 满、写失败、截断以及两槽之一损坏需分别测量。
7. 用外部电流仪记录阅读待机、连续翻页、传书中、停止传书后、FAST、ECO、关机的稳定电流；注明 USB/电池及测点。串口接入本身可能改变休眠与功耗。
8. FAST 恢复原 RAM 编辑；ECO 仅在完整 ACK 后切 EN。失败保持运行并允许重试/返回编辑；突然断电只恢复最近成功点。用受控断电在每个写入阶段注入故障，检查旧数据可恢复性。

建议留存每次样本 CSV，至少包含 `firmware_sha,scenario,sample,input_us,feedback_us,render_us,present_us,wave_transactions,gc16_count,internal_free_min,psram_free_min,current_ua,result`。不存在的测量值留空，不以主机值填充。

OTA 上板前还需单独验证实际置备证据、已安装启动器回滚能力、低电量/断网/取消/写失败、首次启动健康确认以及串口救砖。默认 factory 版会拒绝 OTA；当前服务尚未从产品入口闭环，不能以单独策略测试代替这一项。

## 本地软件边界观测

`CONFIG_FOLIO_METRICS` 默认关闭。关闭时 `folio_metrics.h` 的接口宏不求值实参，不读取时钟、不查询堆、不加锁、不写日志；只为观测构建叠加 `firmware/sdkconfig.metrics`。使用独立 build 与 sdkconfig，避免打开观测后污染普通产品构建：

```sh
bash scripts/build_firmware.sh -B "$PWD/build/metrics" \
  -DSDKCONFIG="$PWD/build/metrics/sdkconfig" \
  '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.metrics'
sh tests/metrics/run.sh
```

前一个命令仅编译。刷写和串口采集仍遵循上面的真机确认步骤；分析器只读取已有本地文件，不打开串口。测试脚本使用真实 `folio_metrics.c`、假的时钟/堆/日志与主机互斥锁生成夹具，再验证分析器；其数据必须标记 `host-fixture`，不是 ESP32-S3 性能。分析已有日志：

```sh
python3 scripts/analyze_metrics.py docs/local/capture.log \
  --evidence hardware-capture --output docs/local/metrics.json
```

只有来自实际设备的采集才传 `hardware-capture`；默认 `unverified`，不会因出现固件标签自动升级证据等级。纯 JSONL 和带时间戳/ANSI 颜色的 ESP 日志均可读取；输出是可比较的 JSON。报告记录坏行及前 20 个行号/原因、序列缺口、重复记录、缺失 session、找不到输入的 span、输入前开始的错误关联、未闭合输入和无 display 输入。无 display 可能是合法的无刷新操作；不能将它补成零延迟。没有样本的百分位为 `null`。日志全无有效记录时退出码为 `2`。

### 埋点边界与解释

启动时、其他采样任务之前调用一次 `folio_metrics_init()`；界面切换后设置固定 `FOLIO_METRICS_*` 场景。调用只在任务上下文使用，不在 ISR 中使用。`folio_metrics_input(scenario, kind)` 必须在已去抖或已识别事件派发前调用，并在所有同步处理/绘制/显示结束后用 `folio_metrics_input_end()` 关闭；没有刷新也要关闭。输入不嵌套，遗漏关闭会在报告中显示 `unclosed_inputs`。切换场景后开始的阶段归入新场景，每次输入的延迟/波形次数则归入输入发起场景。

`folio_metrics_begin()` 在短临界区内捕获单调微秒时钟、场景和输入编号。`render_end(span)`、`display_end(span, ...)` 先取结束时钟，再进行堆查询和串口日志。跨任务的 span 使用它开始时的关联，不会因为新输入到达就被重归类；事件结束后新开始的后台工作使用 `input_id=0`。嵌套调用的各段时间不能直接相加，异步预排版也可能在派发结束之后才完成。

| 报告字段 | 精确定义与边界 |
| --- | --- |
| `render_us` | 已接入的绘制函数开始到返回；不自动包含未接入的排版、预渲染等待或所有后台工作。 |
| `display_us` | 已接入的高层显示调用开始到返回；不等同物理像素稳定时间。入口之外的上电、队列等待或其他调用不在此段内。 |
| `input_to_first_display_begin_us` / `end_us` | 已派发输入到关联的首个显示调用开始/完成；不包含触摸硬件中断到主循环识别前的时间。完成后仍可能有残影或面板响应。 |
| `input_handling_us` | 从派发前埋点到关闭事件；包含中间产生的观测日志开销，不代表异步工作已全部结束。 |
| `wave_transactions` / `gc16_transactions` | 已接入的显示调用尝试数及最终模式低 4 位为 GC16 的次数；包括失败调用，`display_errors` 单列。不能证明驱动内部只执行了一段波形，不能代表未知/未埋点的直接 `epd_clear`、连续 DU 或底层调用。 |
| `waves_per_closed_input` / `gc16_per_closed_input` | 已关闭输入关联的显示尝试次数分布；缺输入/缺关闭的样本不补零。日志缺行仍会使次数偏低。 |
| `min_observed_*_free_bytes` | 每次 render/display 结束后 `INTERNAL\|8BIT` 与 `SPIRAM\|8BIT` 空闲字节快照中的最小值；不是绝对最低堆余量，不是最大连续可分配块。`heap_samples` 给出采样次数。 |

每个 display 原始记录还包含最终模式、官方面板/跟随波形类别、请求的 `[x,y,w,h]` 和驱动返回值。请求区域不表示实际扫描区域，也不用于推算省电比例。日志使用固定 `folio_metrics` 标签和固定枚举，不接收书名、书路径、正文、SSID、UID 或任意自由文本。

所有分布用 nearest-rank p50/p95，并同时输出 `n/min/max`。样本数很少时，p95 通常只是最大观测值；先检查质量字段再比较。同一捕获中的 session 标记分开重启前后的编号，正常并发日志乱序按编号/时间关联，重复编号不会被重复统计。报告不猜测遗漏的事件或波形。

### 观测对系统的干扰

启用后的每个输入/结束事件会产生一行串口日志，每个 render/display 结束会多查询两次堆并产生一行日志；开始采样短暂持有自旋锁。它们会消耗 CPU、栈、串口带宽，并可能增加调度或后续输入等待。结束时间不包含该次结束日志，但后续阶段会受到此前日志影响；不能把开启观测的延迟称为无观测成本的产品延迟。

量化干扰时，用完全相同素材和操作分别测默认关闭与开启观测的固件，借助外部录像测输入到可见反馈，并记录两份镜像 SHA 与配置。耗电必须用外部电流仪单独测量，尤其比较串口连接与断开状态；这套接口不提供电流、光学延迟或绝对内存峰值。当前主机测试只证明格式、关联和分析边界，尚无真机开销数字。
