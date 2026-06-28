# 固件架构重写 + UI 全面升级规划

**分支**: `refactor/architecture-ui-overhaul`
**目标**: 把 3001 行单文件 `main.cpp` + 990 行单字符串 `webui.h` 重构为按领域模块化的 C++ 工程，同时全面升级 UI，并去除低效代码。

## 关键设计决策（已与用户确认）

1. **C++ 风格**：类继承 + `virtual`（DeviceMode 策略、IHvacEncoder）。整体 vtable 开销 < 200 字节，可接受。
2. **配置格式**：保留 `/config.txt` 的 `key=value\n` 文本格式（向后兼容，零迁移成本）。ConfigStore 内部封装。
3. **UI 升级范围**：全面升级——iOS 精细化 + 桌面响应式 + 实时状态徽章 + 动画 + 空状态插画 + OTA 进度条 + SVG sprite + localStorage 缓存。
4. **体积控制**：严格控制，必要时省略特性。OTA slot 限制 ~1016KB（当前固件 ~600KB）。

## 体积控制硬约束

- 不引入 ArduinoJson（保留 key=value）
- 不使用 `std::function` / `std::vector` / `std::map` 等 STL（heap fragmentation + flash 增加）
- 注册表使用 C-style 静态数组（编译期大小）
- 所有 HTML/CSS/JS 使用 `PROGMEM` + `strlen_P` 流式
- 模板克制使用，virtual 已经够用
- 编译优化：`-Os`、LTO、`-DNDEBUG`
- 前端 minify + SVG sprite 复用
- 删除 unused / dead code（每阶段结束 `pio run` 验证）

## 新项目结构

```
firmware/src/
├── main.cpp                        # ~100 行：仅 setup()/loop() 框架
├── app/
│   ├── app_context.{h,cpp}         # 全局上下文（聚合所有子系统）
│   └── mode_factory.{h,cpp}        # DeviceMode 工厂
├── core/
│   ├── pins.h                      # GPIO 集中
│   ├── constants.h                 # 魔数集中（间隔/限制/超时）
│   ├── json_builder.{h,cpp}        # 栈分配 JSON（替代 String +=）
│   └── string_utils.{h,cpp}        # sanitize/parse
├── config/config_store.{h,cpp}     # Config + LittleFS + 延迟保存
├── hw/
│   ├── led_manager.{h,cpp}         # LED 状态机
│   ├── button_manager.{h,cpp}      # GPIO13 复用按键
│   ├── sensor_service.{h,cpp}      # DS18B20 + AM312
│   ├── ir_service.{h,cpp}          # IR 收发互斥 + 捕获缓冲
│   └── hvac/
│       ├── hvac_encoder.h          # IHvacEncoder 接口
│       ├── hvac_registry.{h,cpp}   # vendor→encoder 注册表
│       ├── wahin_encoder.{h,cpp}   # 华凌 R05D
│       ├── gree_encoder.{h,cpp}    # 格力 YBOFB
│       └── irac_encoder.{h,cpp}    # 通用 IRremoteESP8266 适配
├── net/
│   ├── network_manager.{h,cpp}     # WiFi STA/AP/重连统一
│   ├── mqtt_service.{h,cpp}        # PubSubClient + HA discovery
│   └── udp_mesh.{h,cpp}            # 主从 UDP 协议
├── web/
│   ├── web_server.{h,cpp}          # 路由 + Captive Portal + 流式
│   └── api/
│       ├── hvac_api.{h,cpp}        # /api/hvac, /api/hvac/state, /api/send, /api/capture
│       ├── wifi_api.{h,cpp}        # /api/wifi/*
│       ├── mqtt_api.{h,cpp}        # /api/mqtt/config
│       ├── device_api.{h,cpp}      # /api/device/config, /api/system/info, /api/mode/force
│       ├── slaves_api.{h,cpp}      # /api/slaves, /api/slave/config, /api/pair/*
│       ├── sensor_api.{h,cpp}      # /api/sensor
│       └── factory_api.{h,cpp}     # /api/factory/reset
├── ota/ota_manager.{h,cpp}         # rboot + 校验 + 回滚 + manifest
├── modes/
│   ├── device_mode.h               # 抽象基类
│   ├── ap_master_mode.{h,cpp}
│   ├── sta_slave_mode.{h,cpp}
│   └── sta_home_mode.{h,cpp}
└── webui/                          # 按页拆分（替代单块 webui.h）
    ├── webui_root.h                # 主框架 + CSS 主题
    ├── webui_page_ac.h             # 空调页
    ├── webui_page_devices.h        # 设备页
    ├── webui_page_settings.h       # 设置页
    ├── webui_page_learn.h          # 学习页
    ├── webui_page_remote.h         # 遥控页
    └── webui_assets.h              # SVG sprite
```

## 关键抽象

### 1. DeviceMode 策略模式

```cpp
// modes/device_mode.h
enum class DeviceModeId { ApMaster, StaSlave, StaHome };

class DeviceMode {
public:
    virtual void onEnter() = 0;
    virtual void loop() = 0;
    virtual const char* name() const = 0;
    virtual ~DeviceMode() = default;
};
// ApMasterMode / StaSlaveMode / StaHomeMode 子类
// main.cpp::loop() 只剩：mode->loop();
```

启动决策逻辑保留在 `AppContext::decideBootMode()`（读取 force_mode、扫描 WiFi、按现有规则返回 `DeviceModeId`）。`mode_factory.cpp` 根据 ID 创建对应子类。

### 2. HvacEncoder 注册表

```cpp
// hw/hvac/hvac_encoder.h
struct HvacCommand {
    String vendor;   // "GREE" / "WAHIN" / "KELVINATOR" / ...
    bool power;
    String mode;     // Cool/Heat/Dry/Fan/Auto
    int temp;
    String fan;      // Auto/Low/Medium/High/Highest
    String swing;
};

class IHvacEncoder {
public:
    virtual const char* vendor() const = 0;   // 匹配 vendor 字符串
    virtual int minTemp() const = 0;
    virtual int maxTemp() const = 0;
    virtual bool send(const HvacCommand& cmd) = 0;
    virtual ~IHvacEncoder() = default;
};

// hw/hvac/hvac_registry.h
class HvacRegistry {
public:
    static const size_t MAX_ENCODERS = 4;   // GREE/WAHIN/IRac 兜底 + 1 备用
    void begin(IrService& ir);
    bool send(const HvacCommand& cmd);       // 自动分发，未注册走 IRac 兜底
    bool isKnownVendor(const String& vendor) const;
    int minTempFor(const String& vendor) const;
private:
    IHvacEncoder* encoders_[MAX_ENCODERS];
    size_t count_ = 0;
    IrService* ir_ = nullptr;
};
```

新增品牌 = 新建 `xxx_encoder.cpp` 实现接口 + 在 `begin()` 注册一行。`sendHvacCommand` 函数消失。

### 3. HvacState 单一状态源

```cpp
// app/app_context.h
struct HvacState {
    String vendor = "GREE";
    bool power = false;
    String mode = "Cool";
    int temp = 26;
    String fan = "Auto";

    void apply(const HvacCommand& cmd);
    void persistTo(Config& cfg) const;   // 写 cfg.last_*
    void loadFrom(const Config& cfg);    // 读 cfg.last_*
};
```

消除全局 `currentVendor`/`currentPower`/`currentMode`/`currentTemp`/`currentFan` 5 个变量，与 `cfg.last_*` 的双份状态合并为单一源。

### 4. JsonBuilder（栈分配，零 heap）

```cpp
// core/json_builder.h
class JsonBuilder {
public:
    JsonBuilder(char* buf, size_t size);
    void beginObject();
    void endObject();
    void beginArray();
    void endArray();
    void key(const char* k);                // 下一个 kvString/kvInt 前调用
    void kvString(const char* k, const char* v);
    void kvString(const char* k, const String& v);
    void kvInt(const char* k, long v);
    void kvBool(const char* k, bool v);
    void kvRaw(const char* k, const char* raw);   // null/true/false/已转义值
    void appendRaw(const char* raw);               // 在当前对象/数组内追加原始 JSON 片段
    const char* result() const;
    size_t length() const;
private:
    char* buf_;
    size_t size_, pos_;
    bool needComma_;
    void ensure(size_t additional);
    void putComma();
};
```

典型用法（替代 handleWifiStatus 等）：

```cpp
char buf[256];
JsonBuilder jb(buf, sizeof(buf));
jb.beginObject();
jb.kvString("mode", modeString());
jb.kvString("ssid", cfg.sta_ssid);
jb.kvInt("rssi", WiFi.RSSI());
jb.kvBool("mqtt", mqtt.connected());
jb.endObject();
server.send(200, "application/json", jb.result());
```

### 5. AppContext（聚合所有子系统）

```cpp
// app/app_context.h
class AppContext {
public:
    // 子系统（按构造顺序，依赖在前）
    ConfigStore config;
    LedManager leds;
    ButtonManager button;
    SensorService sensors;
    IrService ir;
    HvacRegistry hvac;
    NetworkManager network;
    MqttService mqtt;
    UdpMesh udp;
    WebServer web;       // 包装 ESP8266WebServer + Captive Portal
    OtaManager ota;

    // 状态
    HvacState hvacState;
    DeviceMode* mode = nullptr;
    SlaveRegistry slaves;     // 替代全局 slaves[]
    PairingWindow pairing;

    void begin();             // 初始化所有子系统
    void loop();              // 转发到 mode->loop()
    void changeMode(DeviceModeId id);
    void reboot(uint32_t delayMs = 500);   // 统一重启逻辑
    DeviceModeId decideBootMode();         // 启动决策

    // 缓存（启动时计算一次）
    const String& chipId() const { return chipId_; }
    const String& macAddress() const { return mac_; }
    const String& slaveId() const { return slaveId_; }

private:
    String chipId_, mac_, slaveId_;
};
```

所有模块通过 `AppContext& ctx` 引用互相访问，不再有 141 个 free-floating globals。

### 6. IR 捕获缓冲（去 String 分配）

```cpp
// hw/ir_service.h
class IrService {
public:
    void begin();
    void sendRaw(const uint16_t* timings, size_t len);   // 含 disable/enableIRIn 互斥
    bool captureIfAvailable(CaptureResult& out);          // 输出参数，零 String 分配
    bool hasCapture() const;
    void clearCapture();
private:
    IRsend irSend_;
    IRrecv irRecv_;
    IRac ac_;
    // 固定缓冲替代 String capturedRaw
    static const size_t CAPTURE_BUF_SIZE = 1200;   // 足够 ~200 raw ticks
    char captureBuf_[CAPTURE_BUF_SIZE];
    size_t captureLen_ = 0;
    char captureProto_[16];
    int captureBits_ = 0;
    bool hasCapture_ = false;
};
```

## UI 升级方案

### 设计语言

延续 **iOS Human Interface Guidelines** 风格：
- 毛玻璃导航栏（`backdrop-filter: blur(20px)`）
- 14px 圆角卡片、SF Pro 字体栈
- SF Symbols 风格 SVG 图标
- iOS 系统色板

### 文件组织

`webui/` 目录按页拆分 `PROGMEM`：

- `webui_root.h`：`<!DOCTYPE>` + `<head>`（CSS 主题、暗色模式、SVG sprite）+ 主框架 + `<script>` 主控制器
- `webui_page_ac.h` / `webui_page_devices.h` / `webui_page_settings.h` / `webui_page_learn.h` / `webui_page_remote.h`：每页 HTML 字符串

`handleRoot()` 流式发送：root → 各 page → JS 主控制器。**首次请求全部送出**（不做 fetch 分页，避免 RAM 中拼装响应）。

### 视觉与交互升级

| 项 | 升级 |
|---|---|
| 顶部状态栏 | 显示当前温度（DS18B20）、人体感应状态点、WiFi/MQTT 连接指示 |
| 空调卡片 | 大字号温度、模式/风速 segmented control、向日葵渐变背景表示运行状态 |
| 设备页 | 楼层分组 + 在线/离线状态点 + 长按多选 + 滑动删除 |
| 学习页 | 实时倒计时捕获指示器 + 已学信号波形预览 |
| 设置页 | iOS 风格分组列表 + 详情页跳转 |
| OTA 页 | 实时进度条 + 状态文本（验证/写入/切换/确认）+ 失败时一键回退提示 |
| 空状态 | 每页有空状态插画（SVG）+ 行动按钮引导 |
| 暗色模式 | 三态：跟随系统/强制亮/强制暗（localStorage 保存）+ 色温微调 |
| 响应式 | `< 768px` 单列；`>= 768px` 双列卡片布局 |
| 动画 | 页面切换 fade-in 200ms、卡片 tap scale 0.97、状态徽章 pulse |
| 图标 | SVG sprite `<symbol id="ac">` 集中定义，页面 `<use href="#ac"/>` |
| 缓存 | localStorage 保存最后模式/温度/品牌；离线时仍可发起（重连后重试） |

### 性能优化

- HTML/CSS 全部 minify（去除换行缩进）
- JS 用 IIFE 包裹避免全局污染
- SVG `<symbol>` 复用，避免重复内联
- 路由懒轮询：温度/MQTT 状态 10s 一次；slave 列表 3s 一次；其余按需
- 使用 `requestAnimationFrame` 处理动画，避免 setTimeout 抖动

## 低效代码清单与优化（必做项）

| 问题 | 优化 |
|---|---|
| `String +=` 拼 JSON（10+ 处） | `JsonBuilder` 栈 buffer |
| `capturedRaw = ""` 每次清空 reserve | `IrService` 固定 `char[]` 复用 |
| WiFi 重连两处重复（loop() + slaveLoop()） | `NetworkManager::maintain()` 统一 |
| `currentXxx` + `cfg.last_*` 双份状态 | `HvacState` 单一源 |
| `String msg = String(buf)` 复制 UDP 包 | `UdpMesh` 直接处理 `char*` + 长度 |
| `slaveIdFromMac` 反复构造 | `AppContext::slaveId()` 启动时缓存 |
| `mqttTopicBase()` 每次构造 String | `MqttService` 启动时缓存 base |
| MQTT discovery payload 每次 reserve(900) 拼接 | `PROGMEM` 模板 + 替换 |
| `IRac ac(IR_TX)` 全局即使不用也持有 | `IRacEncoder` 内持有，按需 |
| `setup()` 800 行 + 多次 `delay(500);restart()` | 模式 `onEnter()` + `AppContext::reboot()` |
| Magic numbers（5000/60000/1024/60...） | `core/constants.h` 集中 |
| `loop()` 3 处 `if (deviceMode ==)` 分支 | 策略分发 |

## 向后兼容硬约束（不可破坏）

| 兼容项 | 说明 |
|---|---|
| API 路由 | 所有 `/api/*` 路径、请求参数、响应 JSON 字段**完全保持** |
| UDP 协议 | HELLO/ACK/HVAC/CONFIG/RAW 格式不变 |
| MQTT topic | `homeassistant/climate/ir-ac-{chipId}/config` 及子 topic 不变 |
| LittleFS 路径 | `/config.txt`、`/config.bak`、`/slaves.txt`、`/slaves.bak`、`/ota_state.bin`、`/ota_boots.txt` 不变 |
| 配置文件格式 | `key=value\n` 不变 |
| OTA manifest | `IRACOTA1` 格式不变 |
| GPIO 引脚 | 全部不变 |
| WebUI 交互 | 5 个 Tab 结构、各页面字段保持 |

旧固件 → 新固件升级**不需要擦除配置**，所有已配对的从机/MQTT/网络设置保持有效。

## 迁移阶段（增量验证）

| 阶段 | 内容 | 验证 |
|---|---|---|
| **1. 骨架** | 所有 `.h` 接口 + main.cpp 框架，功能未实现但可编译 | `pio run -e rom0` 编译通过 |
| **2. 核心模块** | ConfigStore / LedManager / ButtonManager / SensorService / IrService / HvacRegistry | 编译 + LSP 干净 |
| **3. 网络与模式** | NetworkManager / UdpMesh / MqttService / 三种 DeviceMode | 编译 + LSP 干净 |
| **4. Web 与 OTA** | WebServer + API + OtaManager | 编译 + LSP 干净 |
| **5. UI 升级** | webui 全面重写 | 编译 + 视觉验证 |
| **6. 清理 + 全环境编译** | 删除旧 main.cpp/webui.h，三环境 `pio run` 全过 | rom0/rom1/factory 通过 |

每阶段交付前：
- `pio run -e rom0` 编译 0 error
- 所有新增/修改文件 LSP 诊断干净
- 没有引入新的 `as any` / `@ts-ignore` 等价物（C++ 侧是 `(void)` 强转或 reinterpret_cast）
- 体积对比：每阶段记录 binary 大小，确保 ≤ +15KB 增量

## 验证方法（最终）

- `pio run -e rom0` / `pio run -e rom1` / `pio run -e factory` 三个环境全部通过
- 全部新增文件 LSP 诊断零 error
- API 行为对比：用 Postman/curl 对比新旧固件所有 `/api/*` 端点的请求/响应字段
- UDP 行为对比：用 netcat 模拟主从通信
- 用户提供硬件实测：首刷 + OTA 升级 + 多设备协同

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 重写引入行为差异 | 每阶段编译 + LSP，最终用户硬件实测 |
| 体积超 OTA slot | 每阶段记录 binary 大小，超阈值砍非核心特性 |
| ESP8266 heap 碎片 | JsonBuilder 栈分配、String 仅在非热路径用 |
| UI 重写破坏既有交互 | 保持 5 Tab 结构与所有字段，仅视觉与交互增强 |
| 模块化过度 | 每个模块必须有明确的"被谁用"列表，避免冗余抽象 |

## 不做项（明确排除）

- 不引入 ArduinoJson / ArduinoJson 等库
- 不重写 rboot-bootloader（保持现状）
- 不修改 `prepare_flash.py` / `make_combined.py`（OTA manifest 格式不变）
- 不改 GPIO 引脚分配
- 不改 OTA 镜像格式（`EA 04` + `IRACOTA1`）
- 不引入前端框架（Vue/React 等）—— 仍是原生 HTML/CSS/JS

---

**等待 Momus 评审反馈后再开始实施。**
