# MQTT 连接类型显式选择 + 巴法云（米家）空调适配计划

## 背景

当前固件通过判断 mqtt_user/mqtt_pass 是否有值来推断 MQTT 连接方式，存在以下问题：
1. WebUI 密码回显 bug（已修复）
2. 无法区分"标准 MQTT"和"巴法云"场景
3. 巴法云空调消息格式未适配，设备无法通过米家语音控制

目标：用户显式选择连接类型，固件按类型走不同逻辑，同时支持直接接入巴法云实现米家语音控制空调。

---

## 一、连接类型定义

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | 自动推断 | 兼容旧配置，按字段有无值走原有逻辑 |
| 1 | 标准 MQTT（HA） | Home Assistant 等标准 MQTT 服务器 |
| 2 | 巴法云（米家） | 米家通过巴法云控制，空调消息格式 `on#模式#温度#风速#左右扫风#上下扫风` |
| 3 | 匿名 | 无认证 MQTT |

---

## 二、各类型字段显示规则

| 连接类型 | 服务器 | 端口 | 用户名 | 密码/私钥 | Topic |
|----------|:------:|:----:|:------:|:---------:|:-----:|
| 标准 MQTT（HA） | ✔ | ✔ | ✔ | ✔ | ✔ |
| 巴法云（米家） | ✔ 预填 `mqtt.bemfa.com` | ✔ 预填 `9501` | 隐藏 | ✔ 标签改为"私钥" | ✔ 标签改为"主题名"，**强制校验必须以 `005` 结尾** |
| 匿名 | ✔ | ✔ | 隐藏 | 隐藏 | ✔ |
| 自动推断 | ✔ | ✔ | ✔ | ✔ | ✔ |

---

## 三、需要改动的文件

### 3.1 `firmware/src/config/config_store.h`

新增字段：
```cpp
struct Config {
    // ... 现有字段 ...
    uint8_t mqtt_type = 0;  // 0=自动推断 1=标准MQTT(HA) 2=巴法云(米家) 3=匿名
    // ...
};
```

### 3.2 `firmware/src/config/config_store.cpp`

- `load()` 中新增 `mqtt_type` 解析行
- `save()` 中新增 `mqtt_type` 写入行

### 3.3 `firmware/src/web/api/web_api.cpp`

- `handleMqttConfig()` GET 响应新增 `type` 字段
- `handleMqttConfig()` POST 处理 `type` 参数
- **Topic 后端校验**：当 `mqtt_type == 2`（巴法云）时，校验 Topic 是否以 `005` 结尾，不匹配则返回 400 错误 `"topic_must_end_with_005"`

### 3.4 `firmware/include/webui.h`

- MQTT 设置页顶部新增下拉框：连接类型
- JS 逻辑：选择类型后 show/hide 对应字段
- 选择"巴法云（米家）"时预填服务器/端口，隐藏用户名，改标签
- **Topic 字段校验**：选择"巴法云（米家）"时，JS 提交前检查 Topic 是否以 `005` 结尾，不匹配则 toast 提示"主题名必须以 005 结尾"并阻止提交

### 3.5 `firmware/src/net/mqtt_service.cpp`

核心改动，分三个子任务：

#### 3.5.1 按 mqtt_type 分支连接

```cpp
bool MqttService::connect() {
    if (strlen(configStore.cfg.mqtt_host) == 0) return false;
    mqtt_.setServer(configStore.cfg.mqtt_host, configStore.cfg.mqtt_port);

    String clientId;
    bool ok;

    switch (configStore.cfg.mqtt_type) {
    case 1:  // 标准 MQTT（HA）
        clientId = String("IR-AC-") + String(ESP.getChipId(), HEX);
        ok = mqtt_.connect(clientId.c_str(),
                           configStore.cfg.mqtt_user,
                           configStore.cfg.mqtt_pass);
        break;
    case 2:  // 巴法云（米家）
        clientId = String(configStore.cfg.mqtt_pass);
        ok = mqtt_.connect(clientId.c_str());
        break;
    case 3:  // 匿名
        clientId = String("IR-AC-") + String(ESP.getChipId(), HEX);
        ok = mqtt_.connect(clientId.c_str());
        break;
    default: // 自动推断（兼容旧逻辑）
        // 保留原有 if/else 分支
        break;
    }

    if (ok) {
        // 订阅通用子 topic（HA 兼容）
        mqtt_.subscribe((topicBase_ + "/mode/set").c_str());
        mqtt_.subscribe((topicBase_ + "/temperature/set").c_str());
        mqtt_.subscribe((topicBase_ + "/fan/set").c_str());
        // 巴法云专用：订阅 {topic}（消息直接发到主题本身，无 /set 后缀）
        if (configStore.cfg.mqtt_type == 2) {
            mqtt_.subscribe(topicBase_.c_str());
        }
        publishDiscovery();
        publishState();
        return true;
    }
    // ...
}
```

#### 3.5.2 订阅巴法云 topic

巴法云空调（005）消息直接发送到 `{topic}` 本身，无需 `/set` 后缀。连接成功后订阅 `{topic}` 即可接收控制指令。

#### 3.5.2a Topic 校验规则

选择"巴法云（米家）"时，WebUI 前端强制校验 Topic 必须以 `005` 结尾：
- 固件的 MQTT 只适配空调（climate 实体），不支持灯泡/插座/风扇等其他巴法云设备类型
- 巴法云根据主题名后三位判定设备类型（`001`=插座 `002`=灯泡 `003`=风扇 `004`=传感器 `005`=空调 `006`=开关 `009`=窗帘）
- 非 `005` 结尾的主题会导致巴法云识别为其他设备类型，消息格式不匹配，米家无法正确控制
- 后端 `web_api.cpp` 的 POST 处理中同样校验，拒绝非法 Topic
- 米家下发空调指令时，巴法云直接将消息推送到 `{topic}` 本身（无 `/set` 后缀）

#### 3.5.3 解析巴法云空调消息格式

巴法云空调（005 结尾主题）的消息格式：
```
开关#模式#温度#风速#左右扫风#上下扫风
```

| 字段 | 含义 | 值 |
|------|------|-----|
| 开关 | 开/关机 | `on` / `off` |
| 模式 | 工作模式 | `1`=自动 `2`=制冷 `3`=制热 `4`=送风 `5`=除湿 `6`=睡眠 `7`=节能 |
| 温度 | 目标温度 | `16`-`32` |
| 风速 | 风速档位 | `0`=自动 `1`-`5` |
| 左右扫风 | 水平扫风 | `1`=开 `0`=关 |
| 上下扫风 | 垂直扫风 | `1`=开 `0`=关 |

示例：
- `on` → 开机
- `off` → 关机
- `on#2` → 制冷模式
- `on#3#20` → 制热 20°C
- `on#2#26#2` → 制冷 26°C 风速 2 档

#### 3.5.4 模式映射

| 巴法云值 | 固件内部模式 |
|----------|-------------|
| `1` | Auto |
| `2` | Cool |
| `3` | Heat |
| `4` | Fan |
| `5` | Dry |
| `6` | Sleep（映射为 Auto，固件不支持睡眠） |
| `7` | Energy（映射为 Auto，固件不支持节能） |

#### 3.5.5 状态发布到巴法云

设备状态需要以巴法云格式发布到 `{topicBase}`，米家才能同步显示。

回传格式与收到的格式一致：
```
on/off#模式值#温度#风速值#左右扫风#上下扫风
```

字段规则同 3.5.3，模式用数值（`1`-`7`），风速用数值（`0`-`5`），扫风用 `1`/`0`。

---

## 四、向后兼容

- `mqtt_type` 默认值为 `0`（自动推断）
- 旧固件升级后配置文件中无 `mqtt_type` 行，load 时跳过，保持默认值 0
- 自动推断模式保留原有 if/else 逻辑，完全兼容旧行为
- 结构体大小增加 1 字节，OTA 后建议恢复出厂一次确保内存对齐

---

## 五、OTA 兼容性

由于 Config 结构体新增了字段，内存布局变化。OTA 升级后：
- 如果旧配置文件格式兼容（文本逐行解析），功能上可正常工作
- 建议首次 OTA 升级后恢复出厂一次，确保配置干净

---

## 六、验证步骤

1. 编译 rom0，生成 combined.bin / ota.bin
2. TTL 刷入 combined.bin
3. WebUI 中选择"巴法云（米家）"，填写私钥和主题名（005 结尾）
4. TTL 确认 `[MQTT] Connected`
5. 米家绑定巴法云账号，语音控制空调
6. 监听 `{topic}` 确认消息格式
7. 验证 HA 模式：切换回"标准 MQTT（HA）"，确认 HA 自动发现正常

---

## 七、待确认事项

- [x] 巴法云空调消息格式：`on#模式#温度#风速#左右扫风#上下扫风`（文档已确认）
- [x] 巴法云空调命令 topic：直接发到 `{topic}` 本身，无 `/set` 后缀
- [x] 状态回传格式：与接收格式一致（`on/off#模式值#温度#风速值#左右扫风#上下扫风`）
- [ ] 巴法云是否支持 QoS 1 以上
- [ ] 米家空调控制是否支持风速/扫风设置（文档提到但需实测）
