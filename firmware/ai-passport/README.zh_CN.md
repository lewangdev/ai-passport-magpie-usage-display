[English](README.md) | [简体中文](README.zh_CN.md)

# AI Passport 版 Magpie Usage Display

面向 FoloToy AI Passport 的 ESP-IDF 5.5.3 固件：ESP32-C3、8 MB Flash、
240×320 ST7789 屏幕，无触摸和 PSRAM。直接复用本仓库的 macOS Companion
和 BLE v4 协议，无需修改 Python 服务；原 Waveshare PlatformIO 固件保留在 `src/`。

## 功能与操作

重新设计了四个竖屏页面：Overview（成功率、调用、错误、额度）、Requests
（模型、HTTP 状态、耗时、TTFT、Token）、Sessions（30 天会话、Token、
估算费用、常用模型）、Context（缓存命中率与 Token 构成）。顶部显示连接
和电量；电量读取失败显示 `--%`。模型名仅展示可打印 ASCII，不支持的字节
以 `?` 替代。Context 使用 Magpie 公共 API 的聚合统计，不提供 App 私有评分。

| 输入 | 行为 |
| --- | --- |
| 上 / 下键单击 | 上一页 / 下一页，四页循环 |
| 确认键单击 | 请求立即刷新 |
| 长按确认键（BSP 默认 500 ms） | 打开 / 关闭快捷操作 |
| 快捷操作中上 / 下 | 选择刷新、聚焦 Magpie、清除配对、返回 |
| 快捷操作中确认 | 执行选择 |
| 清除配对确认页 | 松开后再次长按确认，其他操作取消 |
| 熄屏时按任意键 | 仅唤醒，消耗整个首次按键手势 |

60 秒无操作仅关闭背光，蓝牙和数据同步继续运行，不进入深度睡眠。配对期间
保持亮屏。Companion 默认 15 秒刷新，60 秒未收到有效快照显示 STALE，
90 秒无有效快照则主动断开，让 Companion 重连。新连接收到首包前隐藏旧数据。

## 构建与运行

按[官方安装说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/get-started/index.html)
安装并激活 **ESP-IDF v5.5.3**，在本目录运行：

```bash
source /path/to/esp-idf/export.sh
./tools/validate.sh
```

验证包括使用 ASan/UBSan 的 C 协议和状态测试、与实际 Python 序列化器的
互通测试、Companion 全部测试、隔离配置构建、合并镜像与布局和调试产物身份校验。
也可以分别运行：

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
```

完整固件输出为 `build/FoloToy-AI-Passport-full.bin`，烧录地址 **0x0**。
配套 ELF、MAP、分段镜像及 SHA-256 清单保留在
`build/firmware/<完整固件sha256>/`。验证归档：

```bash
python3 tools/archive_firmware.py verify build/firmware/<完整固件sha256>
```

确认串口并授权完整替换固件后，可执行：

```bash
python -m esptool --chip esp32c3 --port /dev/cu.usbmodemXXXX \
  write_flash 0x0 build/FoloToy-AI-Passport-full.bin
```

合并镜像会替换固件，填充区域可能重置 NVS 和旧配对信息。不要求全片擦除或
原固件回读。若需保留兼容的 NVS，检查分区后使用增量构建和分段
`idf.py -p PORT flash`。不能把仅应用镜像 `FoloToy-AI-Passport.bin` 烧到 0x0。

返回仓库根目录，检查 Magpie 并启动现有 Companion：

```bash
./companion/run.sh --once
./companion/run.sh
```

首次连接时，在 macOS 蓝牙配对框输入屏幕上的六位数字。固件启用带认证的
LE Secure Connections 加密及 NVS 绑定记录，后续复用密钥。某一端丢失密钥时，
通过设备快捷操作清除配对，并移除 macOS 旧配对后重新连接。清除操作只针对
NimBLE 存储，不擦除其他 NVS。仅支持 `refresh` 和 `focus_magpie`，设备不接收
凭据、请求正文或任意 shell 命令。

两种开发板使用相同 BLE 服务和名称以兼容 Companion。现有 Companion 仅支持
单设备，使用时请一次只开启一个显示器。

## 真机验收

编译成功不代表通过真机验证。请在实际 AI Passport 上检查：

1. 色彩、方向、圆角、Logo 和所有文字均在 240×320 可视区内。
2. 上 / 下 / 确认识别，循环翻页、快捷操作、取消及配对清除。
3. 首次配对码、设备和 Mac 重启重连，以及绑定清除后的重配对。
4. 各项指标与 `./companion/run.sh --once` 一致，请求和操作结果能更新。
5. 停止更新后的 STALE、自动重连及断线后的 OFFLINE。
6. 60 秒背光超时，以及各键首次唤醒不会触发其他动作。
7. 电量及电量计缺失时的行为，长时间运行无看门狗复位或内存持续下降。

首次 IDF 构建下载 LVGL 后，可在激活 IDF 的环境生成实际 UI 的主机预览：

```bash
cmake -S tests/render -B build/render -G Ninja \
  -DLVGL_PATH="$PWD/managed_components/lvgl__lvgl"
cmake --build build/render
mkdir -p build/previews
./build/render/render "$PWD/build/previews"
```

该预览使用 LVGL 9.5 和 24 KB 内存池输出 PPM，不模拟 SPI 时序、蓝牙或按键
电压，也不能代替真机测试。

## 来源与结构

`components/bsp/`、`tools/archive_firmware.py`、`tools/verify_firmware.py`、
基础配置、分区及依赖锁来自
[FoloToy/ai-passport 的 3d685a9 版本](https://github.com/FoloToy/ai-passport/tree/3d685a90bdb96dd8526d84edde1e63fec25b460e)。
BSP 原样保留，引脚配置以 `bsp_pins.h` 为准；未链接上游测试菜单及演示页面。
`main/` 为本应用的状态模型、NimBLE 通信和 LVGL 界面。小尺寸 Logo 复用原仓库
位图，仅适配 LVGL 9 描述符。上游 MIT 许可保留于 `LICENSE`，另见仓库第三方
声明；依赖由 `dependencies.lock` 锁定，通过 ESP-IDF 组件管理器获取。

## 真机测试记录 — 2026-10-21

用户授权对 `/dev/cu.usbmodem1101` 完整烧录，并允许重置设置和配对。
本次已验证合并固件 SHA-256：
`ce6d27697b1576dd1c06f3d4237e3683d8f92c97789302565dd779eba1ff192a`；
配套 ELF SHA-256：
`daa97644cb928f8d143f5a64bb3cff3221c468b2970bdd29b5be7a613f08807a`。

- 烧录与 esptool 哈希校验：通过，确认 ESP32-C3、8 MB Flash。
- 首次 25 秒启动观察：通过，预期 ELF 和 ESP-IDF 5.5.3 正常启动；显示、
  LVGL、CW2017、ADC 按键及 BLE 广播初始化完成，未观察到崩溃或看门狗复位。
- 屏幕和基本翻页、按键交互：用户反馈正常。
- 本机 Magpie HTTP 端点：可访问，HTTP 200。
- BLE 配对、数据、快捷操作和重连：尚未验证。Mac 扫描报告蓝牙不可用；
  随后串口观察到设备运行另一份 `nabo-table-duel-offline` 固件
  （ESP-IDF 6.0.2，分区不同）。用户选择保留替换后的固件并停止测试，
  此后未再操作设备；不能假定设备当前仍运行本次 Magpie 固件。
- 背光超时、唤醒、长期内存稳定性与指标准确性：需恢复目标固件后继续验收。

串口监视和临时 Companion 测试均已停止并释放端口；未执行全片擦除、
提交、推送或发布。
