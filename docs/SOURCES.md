# Folio 来源与许可

官方演示固件来自 [MindReset/read_pico_firmware](https://github.com/MindReset/read_pico_firmware)，固定提交 `5f3406deec33304de8801046db583dec7c49e55b`。`firmware/` 最初逐字节导入该提交的 383 个文件。原目录仅作只读参考。

`upstream-manifest.json` 记录每个原文件的路径、字节数和 SHA-256。运行 `python3 scripts/audit_sources.py` 会按当前工作区列出原样复用、修改、新增及缺失文件，避免将上游能力误称为 Folio 原创。

| 范围 | 来源与处理 | 原因 / 许可 |
| --- | --- | --- |
| `firmware/components/read_pico*`、芯片驱动 | 复用官方 BSP、PMU、I²C、TF、输入、传书及离线检索 | 保留已验证板型与协议；见各目录 Apache-2.0 或数据集许可 |
| `firmware/components/epdiy` | 复用官方裁剪版，不调整扫描时序 | LGPL-3.0-or-later；保留原说明与源文件声明 |
| `firmware/components/e0470_epaper_waveform` | 原样复用面板波形 | 保留官方面板参数，不把黑闪优化等同于更改波形 |
| `firmware/main/font`、`main/book` | 复用字形缓存、TXT/EPUB、编码、分页、图片及逐书进度；修改存储安全策略 | Apache-2.0；stb/TJpgDec 等见文件内许可 |
| `firmware/main/settings.*`、`sleep.*`、`app/*` | 在官方生命周期上接入 Folio 数据保护、产品路由和睡眠恢复 | 沿用 Apache-2.0，保留原作者标记 |
| `firmware/main/apps/app_book.c` | 基于官方阅读器增加统计和独立排版草稿状态 | 不重写已经存在的解析器或字体引擎 |
| `firmware/main/apps/app_transfer.c` | 复用官方传书页面，修正字体不包含的范围符号 | UI 显示修正；传输协议本批未重写 |
| `firmware/tools` | 复用官方回归，增加 macOS 链接器兼容和 Folio 字体扫描目录 | 保留解析器内存跟踪和 sanitizer，不因宿主平台差异跳过断言 |
| `firmware/main/folio/`、`apps/app_folio_*` | Folio 新建的统计、检查点、产品 UI、时间、壁纸和更新策略 | 新代码 Apache-2.0；不是官方提供的产品功能 |
| `web/`、`scripts/`、`tests/` | Folio 新建本地网站、发布工具与针对新增行为的回归 | 依赖继续适用其自身许可 |
| `firmware/sdcard/fonts/ChillDuanSansVF.ttf` | 官方带入的完整原版字体 | SIL OFL-1.1，保留 `OFL.txt`；原版 SHA-256 `966286fc44907d4123528e1efb98c08e51fff013cd083140fdad1bf6b593555c` |
| `firmware/main/assets/builtin.ttf` | 官方生成器产生的 UI 子集；新增中文后重新生成 | 修改版名称 Read Pico UI，保留 OFL 版权与许可元数据；不得手改位图/字体二进制 |

产品 UI 以本地已冻结 `ui-redesign/v0.4.2/output` 为设计基线，设计包 SHA-256 为 `dc4fcfafd366840f406bbb968b26f6c35e9bcad2e0563f18123b02de9abc6758`。相较 v0.4.1 仅 FMT-CHECKPOINT、FMT-CP-ERROR、FMT-RESTORE 三张实际像素变化，其余 351 张相同。Folio 实现映射与未覆盖状态见 `PRODUCT_UI.md`；设计稿示例数值不是运行数据，静态设计验收不等于固件或真机验收。

## 分发材料

保留 `firmware/LICENSE` 和各组件声明；发布源码、第三方许可、固定工具链与构建说明。epdiy 为 LGPL 组件，二进制发布材料还应包含相应源代码和允许替换该组件后重新链接的对象/构建材料，不能仅提供不可重建的 `.bin`。未准备完整分发材料前不生成公开 Release。

上游 `components/epdiy/LICENSE` 是许可说明及链接，不含完整 GPL/LGPL 正文，Folio 的 `docs/licenses/` 补充 GNU 官方完整正文，分发时一并保留。stb_truetype/stb_image 双许可及 TJpgDec 许可保留在原文件中；拼音映射的 MIT 声明在 `components/read_pico_search/LICENSE.pypinyin`。

网页开发依赖锁在 `web/package-lock.json`：乐鑫 esptool-js 0.7.0（Apache-2.0），以及 atob-lite 2.0.0（MIT）、pako 2.2.0（MIT 与 Zlib）。构建输出保留随包许可证。FontTools 4.66.1 为生成字体所用 MIT 工具，不嵌入固件。

## 构建版本

ESP-IDF 固定官方 v6.1，提交 `fff9895c82d744c7237be8847347bdd1b07c6643`。依据为官方演示的 AGENTS、锁文件、CI 配置和 HPM 私有头依赖；不是按“最新版”自动升级。`sdkconfig.defaults` 的 120MHz Flash/PSRAM 与 Zbit HPM 保持原值，`sdkconfig.ci` 仅作编译回归，不能冒充真机发布配置。
