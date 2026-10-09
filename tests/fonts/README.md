# 字体角色与生产引擎回归

运行 `scripts/test_fonts.sh [输出目录]`。测试直接编译 `firmware/main/font/ttf_font.c` 和其原有 `stb_truetype.h`，使用仓库中的内置字体与完整 ChillDuanSans VF。替身仅承担平台分配、锁、时钟和 framebuffer 像素写入；变体 `gvar`、轮廓读取、LRU、度量、覆盖率 gamma 都执行生产代码。ASan/UBSan 检查内存和未定义行为，输出 PNG 是生产软件渲染，**不是墨水屏照片**。

## 使用契约

- `UI` 固定为内置子集，默认真实字重 400；`READER` 是已应用字体；`PREVIEW` 保存未应用候选。候选失败、取消或 SD 丢失都不把别的字体默默当作成功结果。
- 初始化时 `ttf_font_init()` 加载 UI，再加载正文；无卡启动可显式回退内置正文子集。恢复排版草稿必须另行校验记录的字体身份，不能把启动回退当恢复成功。
- `ttf_font_prepare(role, path)` 只在事件回调或恢复阶段调用，做文件 I/O。相同身份复用现有 face。`ttf_font_begin(role, &scope)` 只选择已加载上下文并持递归锁，不加载字体。所有旧公开绘制/度量 API 也用相同锁。
- 同任务严格配对、后进先出 `begin/end`。在作用域里使用现有 `ttf_draw_text_px`、`ttf_text_width_px`、`ttf_set_weight` 等 API。若上层还有阅读器绘制锁，固定顺序为 **阅读器绘制锁 → 字体作用域锁**；不反向取锁。后台预渲染必须显式使用 `READER`，预览使用 `PREVIEW`，界面中文使用 `UI`。
- `ttf_font_apply_preview()` 无文件 I/O，把 PREVIEW 与 READER 交换。旧正文留在 PREVIEW，可以再次交换做上层事务回滚；确认排版与持久化事务成功后再 `discard_preview()`。它不保存设置、不分页、不代替持久化回执。
- `ttf_font_media_lost()` / `suspend_sd(true)` 在同一锁下关闭所有外部字体句柄、释放相关缓存、保留逻辑身份并标记不可用；内置 UI 保持。解除暂停不自动重新开文件，上层验证资源后显式 prepare。没有偷偷恢复传书或用内置字体替换待恢复草稿。
- 指针型查询返回当前上下文内部内存；需要跨异步调用保存时应在作用域内复制。`begin` 返回错误时，上层必须显示可恢复提示，不能继续假装正在使用请求的字体。

## 字号、字重与缺字

既有 `*_px` API 保留字体 ascent−descent 像素高度语义。新排版使用 em 时，先在对应角色作用域内调用 `ttf_font_pixel_height_for_em`；此函数按真实 unitsPerEm 和垂直度量换算，不使用固定比例。返回 0 表示该字号超出有界能力；本实现 native 上限 160，涵盖 72 em 正文及更大的章节标题。

官方单 `wght` 轴 gvar 实现和 0.6 覆盖率 gamma 保留。普通静态字体、当前引擎不支持的多轴变化不会伪造字重，`ttf_font_weight_range` 返回 false 并固定 400。源引擎用 hmtx 的 advance；没有增加 HVAR 可变 advance 或塑形引擎，因此测试明确验证当前 ChillDuanSans 的字重改变墨量、度量 advance 保持一致。

内置 UI 子集不覆盖任意书籍正文。`ttf_font_check_text` / `has_codepoint` 检测 cmap，返回首个缺字；不把 glyph 0 当支持。上层必须校验实际预览/正文需要的文字并显示缺字/资源异常，不能仅以字体文件打开成功认定整本书支持。SFNT 目录有文件范围、数量和驻留工作表预算检查；CRC 是字体身份辅助，**不是完整内容安全校验或字体文件的安全认证**。

## 资源预算与回归范围

字形缓存总上限维持 1.5 MiB：UI 128 KiB、READER 1 MiB、PREVIEW 384 KiB。每个新字形先验证单条大小再淘汰 LRU，交换角色后立即收紧旧正文预算。UI 直接从内嵌 ROM 读取轮廓，不复制整表或分配 SD 缓冲；PREVIEW 保留官方 32×4 KiB 扇区缓存及 32 KiB 连续预取缓冲，使用流式读取。准备候选时仅回收正文可选整表映射，保留其文件、度量和字形缓存，避免三份整表争用 PSRAM。

`ttf_font_cache_stats` 返回字形、驻留工作表/loca/gvar 索引、流式缓冲、可选映射和变体 scratch 分项。主机分配计数反映这些生产分配，不包括板上 framebuffer、RTOS、网络、解析器、显示驱动或堆碎片；整机 8 MiB PSRAM 余量仍须固件与真机测量。

测试覆盖：400/800 实际字形区别、em 换算、warm hits、切角色不重开或清缓存、预览失败/取消不影响正文、交换与回滚、UI 隔离、后台线程互斥、缺字、三个预算压力与淘汰、同尺寸文件目录变化、传输暂停/解除、拔卡、截断后的真实读取失败、分配失败和最终释放。输出中的检查数包含文件拷贝断言，不能用这个数字代表产品画板覆盖率或真机性能。
