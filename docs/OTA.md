# Folio 更新、发布包与本地网站

当前实现包括 OTA 准入策略、ESP-IDF 传输适配器、发布打包工具和静态刷机页面。默认仍使用官方 `partitions_16M.csv`；它没有 OTA 槽，设备 OTA 必须拒绝。可选分区文件存在，不表示已授权迁移，也未改变任何真机。

## 分区与首次安装

| 区域 | 官方 factory | 可选 `folio-ota-v1` | 大小 |
| --- | --- | --- | --- |
| NVS | `0x9000` | `0x9000` | 20 KiB |
| PHY | `0xE000` | `0xE000` | 4 KiB |
| factory / ota_0 | `0x10000` | `0x10000` | 4 MiB |
| storage（FAT） | `0x410000` | `0x410000` | 5 MiB |
| ota_1 | 无 | `0x910000` | 4 MiB |
| otadata | 无 | `0xD10000` | 8 KiB |

可选表符合 64 KiB 应用地址对齐、4 KiB 数据地址对齐，所有区域均在 16 MiB Flash 内且不重叠。必须使用 ESP-IDF v6.1 `gen_esp32part.py` 再验证实际构建产物。数据区位置保留仅意味着布局不覆盖它们；应用挂载失败、格式变化或数据迁移仍需独立处理。

首次安装或 factory → OTA 的步骤必须单独获得用户确认，确认内容应列出设备板型、现有/目标分区、实际 bootloader/partition/app 文件和地址、备份位置、覆盖区域、停电恢复方式。先读设备现有分区、备份 NVS/FAT、验证目标 bootloader 已启用回滚，再按**实际构建的 `flash_args`**制定串口命令。本仓库不自动执行这类迁移，不提供自动全片擦除。不得仅把 factory 标签改成 ota_0 就宣布回滚可用。

## OTA 服务接线

`firmware/main/folio/ota_policy.{h,c}` 为无 I/O 的纯 C 策略；`ota_service.{h,c}` 使用 ESP-IDF `app_update`、`esp_http_client`、`mbedtls`。主组件须加入这两个源文件及依赖。默认未开启回滚/证书 bundle 的构建中，适配器编译为拒绝路径。

- `folio_ota_layout_available()` 只核验分区和 Flash 容量；**不能证明物理板型或 bootloader 能力**。
- `folio_ota_confirm_boot(health_bits)` 必须在首屏刷新、输入就绪、设置与内部数据恢复、事件循环运行均成功后调用。可选 SD 卡不存在不等于内部存储健康。缺任一项不确认。待确认启动发生重启时由已正确置备的 IDF bootloader 回滚；本函数不自动重启。
- `folio_ota_install(request, &refusal)` 是独占同步操作，应从专用工作任务调用，不能在渲染或触摸回调内阻塞调用。调用方在任务期间阻止休眠、暂停阅读数据变动，完成后释放按需 WiFi 并让用户决定重启。
- `request.provisioning` 必须返回先前人工明确审核过的**物理板型和实际 bootloader 的回滚能力证据**，包括引导二进制长度与 SHA-256。服务会读取设备实际 bootloader 再核对该摘要。不能从编译宏、下载 manifest 或板型常量伪造“已验证”证据。默认没有这份证据，因此安装失败关闭。
- `request.checkpoint` 要求阅读进度、统计和需要跨断电保留的草稿已成功检查点保存。保存失败拒绝开始。`battery` 返回新鲜有效的千分比电量，未知或低于 300 拒绝；传输每 64 KiB 和切启动槽前再检查。30% 是保守初始策略，非实测电池安全保证。
- `cancelled` 可取消传输；`progress` 在工作任务内回调，不应直接操作 UI。结果由 `esp_err_t` 和策略拒绝码共同报告。

OTA 只访问固定仓库 `https://github.com/mrrobot97/Folio/releases/download/v<规范 x.y.z>/`。清单固定为 `ota.txt`，镜像固定为 `app.bin`。GitHub 资产重定向仅允许 HTTPS `release-assets.githubusercontent.com`，每一跳使用证书 bundle 校验，不关闭主机名验证。离线/时间不正确/TLS 错误/长度未知时中止。

`ota.txt` 是 8 行 UTF-8/ASCII 文本（末尾换行），按顺序为：`FOLIO-OTA-1`、板型、芯片、布局、版本、ESP app project 名、十进制镜像字节数、64 位小写 SHA-256。由打包工具生成，不手填。SHA-256 验证文件完整性；来源信任来自固定仓库的经验证 HTTPS，**不是离线发布签名**。仓库账户失陷不在该机制的保护范围。后续签名密钥/Secure Boot/anti-rollback/eFuse 需要另外评审和明确授权；本实现不配置它们，并拒绝已开启 anti-rollback 的安装路径。

传输写入非运行应用槽，不写 bootloader/partition/NVS/FAT；先核对全量 SHA-256、ESP 镜像校验及嵌入版本/project，再选择启动槽。失败会 abort/清理 HTTP 连接并保留旧启动选择。写入中断可能留下不完整的非活动镜像，下次完整重试会重写；不把断点续传当作已完成。`esp_ota_set_boot_partition` 使用 IDF 的冗余 OTA 选择数据，之后首次启动仍需健康确认。NVS/FAT 中的应用数据必须保持至少前一版本可读，数据格式迁移不能与固件回滚互相破坏。

## 独立 OTA 编译验证（不烧写）

`sdkconfig.ota` 只用于明确选择的额外构建目录。先使用已批准的 ESP-IDF v6.1 环境，再从 `firmware/` 执行：

```sh
idf.py -B build-ota-check -D SDKCONFIG=build-ota-check/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.ci;sdkconfig.ota' -D IDF_TARGET=esp32s3 build
```

该命令保留默认 `sdkconfig` 和默认 `build/`，使用保守 CI 硬件时序，同时启用候选双槽分区、bootloader rollback 和完整 TLS 证书 bundle，使下载、哈希、置备证据和启动选择分支真正参与编译。它是编译检查产物，不是可刷产品配置。若要验证官方 120 MHz 产品时序，将基础项换成 `sdkconfig.defaults` 并使用另一个新构建目录；仍须真机验收和迁移确认。

检查 `build-ota-check/config/sdkconfig.json` 中 `BOOTLOADER_APP_ROLLBACK_ENABLE`、`MBEDTLS_CERTIFICATE_BUNDLE` 为 true，`PARTITION_TABLE_CUSTOM_FILENAME` 指向候选 CSV，且 `BOOTLOADER_APP_ANTI_ROLLBACK` 为 false，再核对地图文件中存在 `folio_ota_install` 的实际传输路径。主组件依赖应为 `app_update`、`esp_http_client`、`mbedtls`；`esp-tls` 随 HTTP 客户端依赖引入。`sdkconfig.ota` 不启用 Secure Boot/Flash Encryption/anti-rollback，也不会自行更改设备。

## 从真实构建生成本地发布包

```sh
python3 scripts/release.py --build firmware/build --output /tmp/folio-0.1.0-check --version 0.1.0 --profile compile-only
```

打包使用 `flasher_args.json` 的角色、文件和地址，同时核对 `flash_files`。读取真实 partition binary 的 MD5/布局，读取 app image 的段校验、追加 SHA-256、芯片、项目、IDF 和嵌入版本。拒绝缺文件、错误地址、目录逃逸、超大镜像、未知分区、加密/安全配置、不匹配版本。`--version` 必须与构建时 `PROJECT_VER` 最终嵌入值一致；不会修改二进制版本。

`--profile production` 要求官方板级 120 MHz Flash/PSRAM 配置；这是配置门槛，不是硬件验收证据。编译测试配置不能刷写。只有 production + factory 布局生成 `web_flash:true`；production + OTA 布局且启用回滚才生成 `ota.txt`。所有包都标记 `publication_ready:false`、`distribution_status:local-only`，保留以下发布阻塞项：用户公开发布授权、真机验收证据、完整许可与对应源码、LGPL 重链接对象/构建材料。脚本只在新目录写文件，不上传、不触碰设备、不覆盖旧包。

`manifest.json`/`SHA256SUMS` 不能单独证明发布者身份。用户应从可信项目 Release 取得完整文件并核对发布的清单摘要。公开发布前按 `docs/SOURCES.md` 完成主体、epdiy LGPL、字体 OFL 和其他第三方声明/源码/重链接材料；不能只附一个源代码仓库链接就视为 LGPL 二进制义务已满足。

## GitHub Pages 网站与串口更新

```sh
node web/build.mjs
node web/preview.mjs
# 打开 http://127.0.0.1:4173
node --test web/test/*.test.mjs
```

静态构建、校验和预览无需安装 npm 包。刷写驱动使用固定 `esptool-js 0.7.0`；用户批准安装依赖后在 `web/` 运行 `npm install --ignore-scripts`，然后重新 build，构建器将本地 bundle 和许可证复制入 dist。驱动缺失时页面继续可用，但连接/刷写禁用；不使用 CDN 隐式下载替代依赖安装。构建不捆绑伪造镜像。

网站的界面图库使用已冻结 v0.4.2 的 H01、R01、R04、ST04、L-WALLPAPER 原始 PNG，来源及 SHA-256 记录在 `web/assets/previews/sources.json`。每张均标“设计预览 v0.4.2”，注明实现边界；它们不是主机渲染或真机截图。缩略图保留原始比例、固定尺寸防止加载跳动、延迟加载，点击可打开原尺寸；窄手机屏幕改为单列。五张原图合计约 403 KiB，没有新增图像生成或压缩依赖。

`web/dist` 为可部署静态目录，资源均使用相对路径，支持 `https://mrrobot97.github.io/Folio/` 子路径。公开 Pages 前须确认仓库可见性、目标地址及 Pages 权限，再选择 GitHub Pages 的 Actions 或分支发布方式；当前没有启用 Pages 或上传产物。禁止把 `.tools`、本地构建缓存、设备备份或用户书籍复制到网站。

用户在网页依次选完整文件 → 校验清单和每个 SHA-256 → 核对实体型号和电量 → 点击连接（可能重启进入下载模式）→ 核对芯片/16 MiB/安全状态/实际 bootloader 与分区摘要/当前应用版本 → 勾选覆盖应用确认 → 点击写入。当前应用版本无法比较或目标版本更低时拒绝；相同版本允许修复重刷。当前仅写 manifest 指定的 factory app，`eraseAll:false`，不会重写引导或分区。写完分块回读并校验 SHA-256，再由用户点击重启。串口芯片识别不能证明实体设备是小纸 Pico，用户仍须核对型号；与板型不匹配不能继续。

Web Serial 需要 HTTPS 或可信 localhost，以及支持该 API 的桌面浏览器。`requestPort()` 仅在用户点击后调用，不自动接管已授权串口。USB 断连/占用、拒绝授权、损坏文件、未知分区/安全配置均显示错误并停止。数据线中断时网页不擦数据、不自动重试；重新连接后按对应布局的串口恢复流程修复。写入回读成功不表示屏幕/触摸/睡眠/数据恢复已通过真机验收。

## 可复跑验证与边界

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ifirmware/main/folio firmware/main/folio/ota_policy.c tests/ota/test_policy.c -o /tmp/folio-ota-policy-test
/tmp/folio-ota-policy-test
python3 -m unittest discover -s tests/ota -p 'test_*.py' -v
node --test web/test/*.test.mjs
```

测试中的合成二进制只存在临时目录，不作为发布产物。以上验证纯策略、manifest、分区范围、镜像完整性、禁写条件和串口调用契约；不替代 IDF 完整编译或设备断电测试。真机验收至少覆盖双槽升级/回滚、首启健康失败、每阶段断电、低电量、空间不足、无网络/重定向/TLS失败、进度/统计/排版恢复和串口救砖。

依据：[ESP-IDF v6.1 OTA](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/ota.html)、[分区表](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/partition-tables.html)、[ESP HTTP Client](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/protocols/esp_http_client.html)、[esptool-js 官方源](https://github.com/espressif/esptool-js)、[Chrome Web Serial](https://developer.chrome.com/docs/capabilities/serial)。
