# GK01 AC Controller

基于吉联科技（GreeLink）GK01 开发板的万能红外空调控制器固件。

多台设备自动组网，手机控制一台主机即可同步驱动所有从机发射红外信号。支持设备管理、定向控制、多选发送，适合办公室多楼层场景。

## 功能

- **空调控制** — 内置 30+ 品牌空调协议（格力/华凌/美的/海尔/大金/三菱/松下等），选品牌调温度一键开机
- **设备管理** — 按楼层分组显示所有设备，点击从机卡片可编辑名称、图标、楼层，远程重启或断开
- **多选控制** — 空调页可勾选多个设备一起控制，或全选，或只选部分
- **定向 IR** — 命令可以发给「所有设备」或「指定从机」，互不干扰
- **远程配置** — 主机远程修改从机的 AP 名称/密码，改完从机自动脱钩成为独立主机
- **配对模式** — 主机开启 60 秒配对窗口，新从机加入需要主机确认
- **红外学习** — 捕获任意遥控器信号，命名保存
- **空调状态持久化** — 温度/模式/风速自动保存，重开页面恢复上次设置
- **MQTT Home Assistant** — STA Home 模式接入家庭路由器，支持 HA Climate 自动发现
- **传感器** — DS18B20 温度 + AM312 人体感应（可选配件）
- **OTA 双分区** — rboot 双固件槽，上传写另一分区，启动失败自动回退
- **恢复出厂** — WebUI 一键恢复，或短路 D7-GND 焊盘 5 秒
- **深色模式** — 跟随系统自动切换
- **iOS 风格界面** — 手机端优化，添加到主屏幕变成独立 App

## 界面预览

### 设备管理

![设备页面](screenshots/devices-page.png)

按楼层分组显示所有设备，点击从机卡片可编辑名称、图标、楼层，远程重启或断开。

### 空调控制

![空调页面](screenshots/ac-page.png)

多选控制目标（全选/部分/单个），设置自动保存，下次打开恢复上次状态。

## 硬件

| 项目 | 参数 |
|------|------|
| 板子型号 | GK01（IR Mini V105 圆形 PCB） |
| 厂商 | 吉联科技 / GreeLink |
| 主控 | ESP-12F (ESP8266, 80MHz, 4MB Flash) |
| 红外发射 | 8× IR LED（GPIO14/D5） |
| 红外接收 | VS1838B（GPIO5/D1） |
| 指示灯 | 蓝色 GPIO4/D2, 红色 GPIO12/D6, 黄色 GPIO13/D7 |
| 恢复出厂 | 短路 GPIO13/D7 焊盘到 GND 5 秒（与黄色 LED 复用引脚） |
| 温度传感器 | DS18B20（GPIO2/D4，1-Wire，可选） |
| 人体传感器 | AM312 PIR（GPIO16/D0，可选） |
| 供电 | Micro USB（仅供电）或焊盘 VCC/GND |
| 刷写接口 | 焊盘 TX/RX/GND/IO0，需 USB-TTL 转换器 |

## 快速开始

### 1. 编译固件

需要 [PlatformIO](https://platformio.org/)：

```bash
cd firmware
pio run -e rom0
```

编译产物在 `.pio/build/rom0/firmware.bin`。

也可以从 [GitHub Releases](https://github.com/Timeink88/GK01-AC-Controller/releases) 下载预编译固件（`combined-vX.X.bin`）。

### 2. 刷写固件

需要 USB-TTL 转换器（如 CH340），连接板子焊盘：

| USB-TTL | 板子焊盘 |
|---------|---------|
| TX | RX |
| RX | TX |
| GND | GND |

**进入刷写模式**：短接 IO0 焊盘和 GND 焊盘，然后上电。

```bash
# 擦除
python -m esptool --port COMx --baud 115200 --before no-reset erase-flash

# 刷写（推荐 combined.bin，一条命令刷完）
python -m esptool --port COMx --baud 115200 --before no-reset write-flash -fm dout 0x0 combined-vX.X.bin
```

> `-fm dout` 是必须的，用 `dio`/`qio` 会失败。板子没有 FLASH 按键，必须短接 IO0-GND 才能进入刷写模式。Micro USB 仅供电，没有数据线。

### 3. 使用

1. 刷写完成后拔插电源重启
2. 手机搜索 WiFi（默认 `IR-AC-XXXX`，每台设备唯一），密码 `12345678`
3. 连接后自动弹出控制页面（Captive Portal）
4. 首页「设备」页查看已连接的从机，可改名/设图标/分楼层
5. 「空调」页选择品牌、温度、目标设备，一键控制
6. iOS 用户：Safari → 分享 → 添加到主屏幕

## 多设备协同

### 设备角色

| 角色 | 模式 | 说明 |
|------|------|------|
| 主机 | AP Master | 创建热点、运行 WebUI、广播 IR 命令给从机 |
| 从机 | STA Slave | 连接主机热点、监听 UDP、执行 IR 命令 |
| Home | STA Home | 连接家庭路由器、MQTT 接入 Home Assistant |

### 启动决策

```
上电
  │
  ├─ force_mode = AP → 直接开 AP 主机
  ├─ force_mode = Slave → 只扫描，找不到重试
  ├─ force_mode = Home → 只连路由器，失败重试
  │
  └─ force_mode = Auto（默认）
       ├─ 有 STA 配置 → 连家庭路由器 → 成功则 Home 模式
       ├─ 扫描 IR-AC-* → 找到信号最强的主机 → 从机模式
       └─ 都没有 → AP 主机模式
```

> 设备默认 SSID 为 `IR-AC-XXXX`（XXXX = 芯片 ID 后 4 位），每台唯一，避免不同设备串扰。可通过 WebUI 自定义。

### UDP 协议

| 消息 | 方向 | 格式 |
|------|------|------|
| HELLO | 从机→主机 | `HELLO:<mac>:<name>:<icon>:<floor>`（每 30 秒心跳） |
| ACK | 主机→从机 | `ACK:`（确认 JOIN/HELLO） |
| HVAC | 主机→从机 | `HVAC:<target>:<vendor>,<power>,<mode>,<temp>,<fan>,<swing>` |
| CONFIG | 主机→从机 | `CONFIG:<id>:<key>=<value>`（远程改从机配置） |
| CONFIGACK | 从机→主机 | `CONFIGACK:<id>:<status>` |
| RAW | 主机→从机 | `RAW:<len>:<data>` |

**HVAC target 格式**：
- `ALL` — 所有从机执行
- `A4CF` — 只有指定从机执行
- `A4CF,B827` — 多个从机执行（逗号分隔）

### 配对流程

1. 手机连主机 → 设备页 → 点「配对新设备」
2. 主机开启 60 秒配对窗口
3. 新从机上电 → 扫描 `IR-AC-*` → 连信号最强的主机
4. 从机发送 HELLO → 主机记录 MAC + 名称 + 图标 + 楼层
5. 60 秒后配对窗口关闭，未配对设备不再被接受

### 从机管理

从「设备」页点击任意从机卡片：
- **改名/图标/楼层** — 通过 CONFIG 协议远程修改，从机存入自己的 LittleFS
- **远程重启** — 发送 `CONFIG:<id>:reboot`
- **断开从机** — 发送 `CONFIG:<id>:disconnect`，从机切换为独立主机

## 网络配置

| 参数 | 默认值 | 说明 |
|------|--------|------|
| WiFi 名称 | `IR-AC-XXXX` | 芯片 ID 唯一，可通过 WebUI 修改 |
| WiFi 密码 | `12345678` | 可通过 WebUI 修改，至少 8 位 |
| 设备 IP | `10.1.1.1` | AP 模式固定 IP |
| UDP 端口 | `8888` | 主从通信端口 |

> WebUI「设置 → 热点配置」可自定义 SSID 和密码。WebUI「设置 → 设备模式」可强制指定 AP/Slave/Home/Auto 模式。

## GPIO 引脚分配

| GPIO | NodeMCU | 功能 | 说明 |
|------|---------|------|------|
| 14 | D5 | IR 发射 | 8× 红外 LED，低电平有效 |
| 5 | D1 | IR 接收 | VS1838B 红外接收器 |
| 4 | D2 | 蓝色 LED | 系统状态指示 |
| 12 | D6 | 红色 LED | IR 活动指示 |
| 13 | D7 | 黄色 LED | 状态指示；恢复出厂时短路到 GND 5 秒 |
| 2 | D4 | DS18B20 | 1-Wire 温度传感器（可选） |
| 16 | D0 | AM312 PIR | 人体感应传感器（可选） |
| 0 | IO0 | 刷写模式 | 短接 GND 进入刷写模式 |

## 页面说明

| 页面 | 功能 |
|------|------|
| 设备 | 设备拓扑/楼层分组，从机管理（命名/图标/楼层/远程配置） |
| 空调 | 品牌/温度/模式/风速控制，多选目标设备 |
| 学习 | 捕获遥控器信号，命名保存 |
| 遥控 | 发送已保存的信号，支持编辑/删除 |
| 设置 | 网络/MQTT/热点/设备模式，OTA 更新，恢复出厂 |

## 空调品牌

内置支持 30+ 空调协议。以下品牌使用自研编码器：

| 品牌 | 协议名 | 说明 |
|------|--------|------|
| 华凌 | `WAHIN` | R05D 协议，Gray 码温度编码 |
| 格力 | `GREE` | YBOFB 遥控器，双帧 A+B |

其余品牌（美的/海尔/大金/三菱/松下等）通过 [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) 库支持。

> 内置品牌无法控制时，使用「学习」功能捕获遥控器原始信号。

## API

| 接口 | 方法 | 说明 |
|------|------|------|
| `/` | GET | WebUI 页面 |
| `/api/hvac` | POST | 空调控制（支持 `target` 参数多选） |
| `/api/hvac/state` | GET | 获取上次空调设置（vendor/mode/temp/fan） |
| `/api/send` | POST | 发送原始红外信号 |
| `/api/capture` | GET | 获取最近捕获信号 |
| `/api/wifi/scan` | GET | 扫描 WiFi 网络 |
| `/api/wifi/connect` | POST | 连接 WiFi 并重启 |
| `/api/wifi/status` | GET | 当前网络状态 |
| `/api/wifi/forget` | POST | 清除 WiFi 配置并重启 |
| `/api/ap/config` | GET/POST | 读取/保存 AP 热点配置 |
| `/api/mqtt/config` | GET/POST | 读取/保存 MQTT 配置 |
| `/api/mode/force` | POST | 强制设备模式（auto/ap/slave/home） |
| `/api/device/config` | GET/POST | 本机设备名称/图标/楼层 |
| `/api/slaves` | GET | 已连接从机列表（含名称/图标/楼层） |
| `/api/slave/config` | POST | 远程配置指定从机（name/icon/floor/ap_ssid/reboot/disconnect） |
| `/api/pair/start` | POST | 开启 60 秒配对窗口 |
| `/api/pair/stop` | POST | 关闭配对窗口 |
| `/api/system/info` | GET | 设备信息（MAC/ChipID/模式/运行时间） |
| `/api/factory/reset` | POST | 恢复出厂设置 |
| `/api/sensor` | GET | 温度和人体传感器状态 |
| `/api/ota/status` | GET | 当前 ROM 分区状态 |
| `/update` | POST | OTA 固件上传 |

## 项目结构

```
├── firmware/
│   ├── platformio.ini           # PlatformIO 配置（rboot 双分区 OTA）
│   ├── src/main.cpp             # 主程序（AP/STA/Home 三模式 + 设备管理 + IR + UDP + MQTT）
│   ├── include/webui.h          # iOS 风格 WebUI（PROGMEM 内嵌，5 Tab）
│   ├── prepare_flash.py         # 剥 eboot 头生成 rboot 刷写镜像
│   ├── rboot-bootloader/        # rboot 双分区引导器（MIT）
│   └── ld/                      # rboot 链接脚本（ROM0/ROM1）
├── scripts/
│   └── make_combined.py         # 合并 rboot + ROM0 生成单文件刷写镜像
├── screenshots/                 # WebUI 界面截图
├── .github/workflows/
│   ├── ci.yml                   # push 自动编译验证
│   └── release.yml              # tag 自动编译 + 发 GitHub Release
└── README.md
```

## 技术栈

- **框架**：Arduino (ESP8266, espressif8266@4.2.1)
- **IR 库**：[IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) v2.8.6+
- **MQTT**：[PubSubClient](https://github.com/knolleary/pubsubclient)（Home Assistant 集成）
- **传感器**：DallasTemperature + OneWire（DS18B20）
- **Web 服务器**：ESP8266WebServer
- **DNS**：DNSServer（Captive Portal）
- **设备通信**：WiFi UDP（HELLO/ACK/CONFIG/HVAC/RAW 协议）
- **OTA**：rboot 双分区 + ESP8266HTTPUpdateServer
- **存储**：LittleFS（配置 + OTA 状态）
- **前端**：原生 HTML/CSS/JS，无框架

## CI/CD

推送到 `master`/`dav` 时 GitHub Actions 自动编译验证。打 `v*` tag 时自动编译 4 个固件文件并发 GitHub Release：

| 文件 | 用途 |
|------|------|
| `combined-vX.X.bin` | ⭐ 首选，一条 esptool 命令刷完 |
| `firmware-rom0-vX.X.bin` | 备用（含 eboot 头） |
| `firmware-rom1-vX.X.bin` | OTA 升级用 |
| `rboot-vX.X.bin` | 仅引导器 |

## 版本历史

| 版本 | 说明 |
|------|------|
| v1.3 | 初始版本，基础空调控制 + 学习 + 协同 |
| v1.4 | 网络配置宏化、AP 自动选频、按键触发 IR、灯光 bug 修复 |
| v1.5 | 华凌空调自定义编码器（R05D/Gray码）、格力 YBOFB 自定义编码器、Captive Portal |
| v2.0 | WiFi STA + MQTT Home Assistant + DS18B20 温度 + AM312 人体传感器 + rboot 双分区 OTA |
| v2.2 | AP 热点名称/密码可配置、从机自适应组网、短路 D7-GND 恢复出厂、WebUI 恢复出厂设置 |
| v2.3 | 设备唯一 SSID（IR-AC-XXXX）、强制模式切换、从机列表+配对模式、WebUI 显示 MAC |
| v2.4 | 设备管理（楼层分组/命名/图标）、多选控制、定向 IR、远程配置从机、空调状态持久化、深色模式 |

## 许可证

MIT
