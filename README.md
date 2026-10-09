# Folio

面向小纸 Pico（Read Pico / RDP-G01-W）的阅读固件开发仓库，基于官方演示固件构建，保留 ESP32-S3、16 MiB Flash、8 MiB PSRAM 和官方面板波形支持。界面参考冻结设计 v0.4.2。

当前开发节点包含真实首页导航、离线阅读统计/月历/分书统计、按书字号草稿与检查点、FAST/ECO 保存门槛、自定义 TF 壁纸、手动校时、显式内置存储恢复，以及候选 OTA 策略和本地网页刷机工程。完整排版、书签及全部设计子页仍在开发；详细覆盖和限制见 [产品界面](docs/PRODUCT_UI.md)。尚未进行真机刷写、耗电或墨水屏验收。

## 构建与测试

需要官方 **ESP-IDF v6.1**。下面的安装脚本只写仓库内 `.tools` 和 `.cache`，不会修改 shell 配置；首次需下载编译器等依赖并预留约 4–6 GB。

```bash
bash scripts/bootstrap_idf.sh
bash scripts/build_firmware.sh
bash scripts/test_system.sh
sh scripts/test_reader.sh
sh tests/power/run.sh
sh tests/ui/run.sh
python3 -m unittest discover -s tests/ota -p 'test_*.py'
npm --prefix web test
```

构建输出在 `firmware/build/`。默认保持官方 factory 分区和板级时序。`sdkconfig.ci` 只用于编译检查；候选 `sdkconfig.ota` 必须使用独立构建目录，不应据此直接迁移设备。

## 网页与壁纸

```bash
npm --prefix web install --ignore-scripts
npm --prefix web run build
npm --prefix web run preview
```

网页目前本地运行，未启用 GitHub Pages。它不会自动连接串口，也不会自动擦除设备；只有经过包/设备核对及用户确认后才允许应用更新。没有真实发布包时不能刷写。详见 [OTA 与网页刷机](docs/OTA.md)。

壁纸转换器使用 Pillow，将本地图片裁切成竖屏 684×1216、16 灰阶并附完整性校验：

```bash
python3 scripts/convert_wallpaper.py picture.png wallpaper.fwp
```

将生成文件放在 TF 卡根目录的 `wallpapers/`，在设备设置中预览并应用。预览不会改变已应用选择；损坏或缺失文件使用内置壁纸并说明原因。

## 数据与发布边界

NVS 初始化失败和 FAT 挂载失败均不会自动擦除。阅读进度、统计和未应用草稿分别检查保存结果；ECO/OFF 需要成功的持久化确认。FAST 保留运行内存，突然断电只能恢复最后成功的检查点。

代码来源、许可证、字体和复用/改写边界见 [来源账本](docs/SOURCES.md)；Flash 布局和增长预算见 [容量说明](docs/CAPACITY.md)。公开 Release、Pages 启用、真机刷写、分区迁移和安全配置变更须单独确认。
