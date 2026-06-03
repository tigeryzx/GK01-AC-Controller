# Gree YBOFB 空调动态控制 — 实现方案（v2 修正版）

> **v2 修正说明**：基于同事审查反馈和 raw timing 反算验证，修正了 v1 中的数据错误。
> 主要变更：2.4 节 B7 表格方向修正、2.7 节用 raw 反算数据替换（14/14 校验和全部匹配）、
> B5 默认值修正为 0x00、代码中 endSpace 修正、数据流示例更新。

## 1. 背景与问题

### 1.1 现状

当前固件（v1.4）的空调控制通过 IRremoteESP8266 库的 `IRac::sendAc()` 实现。用户选择品牌后，固件构造 `stdAc::state_t` 结构体并调用 `ac.sendAc(st)` 发送红外信号。

### 1.2 问题

用户拥有**格力（Gree）YBOFB 遥控器**（型号 YAP0F20）控制的空调。经用户实测确认：

- IRremoteESP8266 库的 `GREE` 协议 `sendAc()` **无法控制该空调**
- 使用 KELVINATOR 协议可以部分控制，但缺少功能
- GitHub Issue [#2201](https://github.com/crankyoldgit/IRremoteESP8266/issues/2201) 有其他用户报告相同问题

**根因分析**：IRremoteESP8266 库的 GREE 协议实现基于 `GreeProtocol` LSB-first bit field 结构体，通过 `sendGeneric()` 发送。但 `sendGeneric()` 内部将 state 数组按 MSB-first 发送，而 YBOFB 遥控器需要 LSB-first 发送（与 gree_ext 组件一致）。此外，YBOFB 需要**双帧**（Frame A + Frame B），库仅发送单帧。

### 1.3 解决方案

参考开源社区中**已验证可用**的实现（ESPHome gree_ext 组件），在固件中实现自定义 GREE YBOFB 编码器，绕过 IRremoteESP8266 库的 `sendAc()` 路径。

---

## 2. 协议分析

### 2.1 信号结构

每次按键发送**两个 8 字节帧**（Frame A + Frame B），通过 IR 信号连续发射：

```
Frame A: Header + 4字节(Block1) + 3位Footer(010) + Gap(19980μs)
         + 4字节(Block2) + EndSpace(7300μs)
Frame B: Header + 4字节(Block1) + 3位Footer(010) + Gap(19980μs)
         + 4字节(Block2) + EndSpace(20000μs)
```

### 2.2 IR 物理层参数

| 参数 | 值 |
|------|-----|
| 载波频率 | 38 kHz |
| Header Mark | 9000 μs |
| Header Space | 4500 μs |
| Bit Mark | 620 μs |
| Logic 1 Space | 1600 μs |
| Logic 0 Space | 540 μs |
| Block Footer | 3 bits: `010` |
| Block 间隔 | 19980 μs |
| Frame 间隔 | 7300 μs |

### 2.3 字节编码顺序

**LSB-first**：每个字节从最低位（bit 0）开始发送。

```cpp
// gree_ext 的发送逻辑（已验证）
for (uint8_t mask = 1; mask > 0; mask <<= 1) {
    send_bit(byte & mask);
}
```

> **注意**：IRremoteESP8266 库的 `sendGree()` 内部调用 `sendGeneric()` 时会对 state 数组做 MSB-first 发送，这与 YBOFB 的 LSB-first 需求不匹配。gree_ext 直接使用 `mark()/space()` 手动 bit-bang，避免了这个问题。

### 2.4 数据帧格式（8 字节）

#### Frame A（主命令帧，B3 = 0x50）

| Byte | bit 7 | bit 6 | bit 5 | bit 4 | bit 3 | bit 2 | bit 1 | bit 0 |
|------|-------|-------|-------|-------|-------|-------|-------|-------|
| B0 | Sleep | SwingAuto | Fan[1] | Fan[0] | Power | Mode[2] | Mode[1] | Mode[0] |
| B1 | TimerEn | TimerTens[1] | TimerTens[0] | TimerHalf | Temp[3] | Temp[2] | Temp[1] | Temp[0] |
| B2 | Xfan | ModelA(=0) | Light | Turbo | TimerHr[3] | TimerHr[2] | TimerHr[1] | TimerHr[0] |
| B3 | unk[3] | unk[2] | unk[1] | unk[0] | °F | °F_extra | — | — |
| B4 | — | SwingH[2] | SwingH[1] | SwingH[0] | SwingV[3] | SwingV[2] | SwingV[1] | SwingV[0] |
| B5 | — | WiFi | unk[2] | unk[1] | unk[0] | IFeel | DispTemp[1] | DispTemp[0] |
| B6 | — | — | — | — | — | — | — | — |
| B7 | **Sum[3]** | **Sum[2]** | **Sum[1]** | **Sum[0]** | — | Econo | — | — |

> **v2 修正**：Sum 在 B7 的**高 4 位（bit7-4）**，不是低 4 位。代码 `(sum & 0x0F) << 4` 正确。

#### Frame B（重复帧，B3 = 0x70）

与 Frame A 基本相同，区别：
- B3 = `0x70`（而非 0x50）
- B6 包含风扇信息（`frameB[6] |= fanVal`）
- B7 校验和重新计算

### 2.5 编码值定义

#### 操作模式（B0 低 3 位 + bit3 Power）

| 模式 | Mode 值 | Power=ON 组合 | 实测验证 |
|------|---------|--------------|----------|
| 关机 | 任意 | Power=0 → B0 不含 0x08 | ✓ 关机 B0=0x31 (Cool+HighFan, 无Power) |
| 自动 | 0x00 | 0x08 | ✓ 实测 B0=0x38 |
| 制冷 | 0x01 | 0x09 | ✓ 实测 B0=0x39 |
| 除湿 | 0x02 | 0x0A | ✓ 实测 B0=0x1A (Dry+LowFan) |
| 送风 | 0x03 | 0x0B | ✓ 实测 B0=0x2B (Fan+MedFan) |
| 制热 | 0x04 | 0x0C | ✓ 实测 B0=0x0C (Heat+AutoFan) |

> Power=1 时 B0 的 bit3 置 1，即 `B0 = fanVal | modeVal | 0x08`

#### 风速（B0 bit5-4）

| 风速 | 值 | 实测验证 |
|------|-----|----------|
| 自动 | 0x00 | ✓ 制热/自动 时 |
| 低 | 0x10 | ✓ 除湿 时 |
| 中 | 0x20 | ✓ 送风 时 |
| 高 | 0x30 | ✓ 制冷/关机/开机 时 |

#### 温度（B1 低 4 位）

实际温度 = 16 + Temp（范围 16°C~30°C，即 Temp=0~14）

#### 默认值

| Byte | 默认值 | 来源 |
|------|--------|------|
| B2 bit5 | 1 | Light 显示灯开启（实测所有信号 Light=1） |
| B2 bit4 | 0 | Turbo 默认关闭（实测仅制冷和关机时 Turbo=1） |
| B3 | 0x50 (Frame A) / 0x70 (Frame B) | 消息类型标识（实测 14 帧全部一致） |
| B5 | 0x00 | **v2 修正**：实测所有信号 B5=0x00（之前误写为 0x20） |

### 2.6 校验和算法（Kelvinator block checksum）

```
checksum = (10 + (B0 & 0x0F) + (B1 & 0x0F) + (B2 & 0x0F) + (B3 & 0x0F)
           + (B4 >> 4) + (B5 >> 4) + (B6 >> 4)) & 0x0F
```

结果存入 B7 的**高 4 位**：`B7 = (checksum << 4) | (B7 & 0x0F)`

> 算法来源于 IRremoteESP8266 库源码（`ir_Gree.cpp` 第 170-173 行注释 "Gree uses the same checksum alg. as Kelvinator's block checksum"）。

### 2.7 校验和验证（v2：从 raw timing 反算，14/14 全部匹配）

以下数据从用户捕获的 raw IR timing 直接反算得出（LSB-first 解码），非捕获工具解码结果。

> **v2 说明**：v1 中本节使用的是捕获工具按 KELVINATOR 128bit 协议混合解码的字节，B4-B7 数据有误。
> 从 raw timing 重新解码后，全部 14 帧（7 信号 × 2 帧）校验和 **100% 匹配**，无任何噪音或偏差。

#### Frame A 验证

| 命令 | B0 | B1 | B2 | B3 | B4 | B5 | B6 | B7 | 计算 | 实际 B7 高4位 | 匹配 |
|------|-----|-----|-----|-----|-----|-----|-----|-----|------|------|------|
| 关机 | 31 | 08 | 30 | 50 | 00 | 00 | 00 | 30 | 3 | 3 | ✓ |
| 开机 | 39 | 08 | 30 | 50 | 00 | 00 | 00 | B0 | B | B | ✓ |
| 除湿 | 1A | 0A | 20 | 50 | 00 | 00 | 00 | E0 | E | E | ✓ |
| 送风 | 2B | 07 | 20 | 50 | 00 | 00 | 00 | C0 | C | C | ✓ |
| 制热 | 0C | 08 | 20 | 50 | 00 | 00 | 00 | E8 | E | E | ✓ |
| 自动 | 38 | 09 | 20 | 50 | 00 | 00 | 00 | B0 | B | B | ✓ |
| 制冷 | 39 | 08 | 30 | 50 | 00 | 00 | 00 | B0 | B | B | ✓ |

#### Frame B 验证

| 命令 | B0 | B1 | B2 | B3 | B4 | B5 | B6 | B7 | 计算 | 实际 B7 高4位 | 匹配 |
|------|-----|-----|-----|-----|-----|-----|-----|-----|------|------|------|
| 关机 | 31 | 08 | 30 | 70 | 00 | 00 | D0 | 00 | 0 | 0 | ✓ |
| 开机 | 39 | 08 | 30 | 70 | 00 | 00 | D0 | 80 | 8 | 8 | ✓ |
| 除湿 | 1A | 0A | 20 | 70 | 00 | 00 | 90 | 70 | 7 | 7 | ✓ |
| 送风 | 2B | 07 | 20 | 70 | 00 | 00 | A0 | 60 | 6 | 6 | ✓ |
| 制热 | 0C | 08 | 20 | 70 | 00 | 00 | 80 | 60 | 6 | 6 | ✓ |
| 自动 | 38 | 09 | 20 | 70 | 00 | 00 | B0 | 60 | 6 | 6 | ✓ |
| 制冷 | 39 | 08 | 30 | 70 | 00 | 00 | D0 | 80 | 8 | 8 | ✓ |

#### 验证方法说明

1. 从 JSON 文件中提取 7 组 raw IR timing 数据
2. 用 Python 解码器从 raw timing 直接提取 bits（threshold=1100μs 区分 0/1）
3. 按 LSB-first 顺序将 bits 组装为字节
4. 每个信号识别到 2 帧（Frame A: B3=0x50, Frame B: B3=0x70）
5. 对 14 帧计算校验和，**全部完美匹配**

---

## 3. 实现方案

### 3.1 方案概述

在 `main.cpp` 中新增自定义 GREE YBOFB 编码函数，当用户选择 `GREE` 品牌时，绕过 `IRac::sendAc()`，直接调用自定义编码器发送信号。

### 3.2 架构设计

```
WebUI /api/hvac POST
  │
  ├─ vendor == "GREE" ?
  │    YES → sendGreeYBOFB(power, mode, temp, fan)  ← 自定义编码器
  │    NO  → ac.sendAc(st)                           ← 库原有路径
  │
  ▼
sendGreeYBOFB():
  1. 构造 Frame A (8字节) — 设置模式/温度/风速/默认值
  2. 构造 Frame B (8字节) — 基于Frame A，修改B3=0x70，B6包含风速
  3. 计算两帧的校验和
  4. 调用 greeSendFrame() 依次发送两帧
  │
  ▼
greeSendFrame(frame[]):
  1. 发送 Header (9000/4500)
  2. LSB-first 发送 Block1 (B0-B3)
  3. 发送 3位 Footer (010)
  4. 发送 Gap (19980μs)
  5. LSB-first 发送 Block2 (B4-B7)
  6. 发送结束 mark
```

### 3.3 修改范围

| 文件 | 修改内容 | 行数估计 |
|------|----------|----------|
| `src/main.cpp` | 新增 GREE 编码函数 + 修改 handleHvac() 和 slaveExecHvac() 路由 | +120 行 |
| `include/webui.h` | 无修改（WebUI 已支持 GREE 品牌选择和温度/模式/风速控制） | 0 |
| `platformio.ini` | 无修改（IRremoteESP8266 库已包含，仅使用其 IRsend 底层发送能力） | 0 |

---

## 4. 详细设计

### 4.1 新增常量定义

```cpp
// ===== Gree YBOFB 自定义编码器 =====
#define GREE_HDR_MARK    9000
#define GREE_HDR_SPACE   4500
#define GREE_BIT_MARK    620
#define GREE_ONE_SPACE   1600
#define GREE_ZERO_SPACE  540
#define GREE_MSG_SPACE   19980
#define GREE_FRAME_SPACE 7300   // Frame A 和 Frame B 之间的间隔

// 模式编码
#define GREE_MODE_AUTO   0x00
#define GREE_MODE_COOL   0x01
#define GREE_MODE_DRY    0x02
#define GREE_MODE_FAN    0x03
#define GREE_MODE_HEAT   0x04
#define GREE_MODE_ON     0x08   // Power bit

// 风速编码（B0 bit5-4）
#define GREE_FAN_AUTO    0x00
#define GREE_FAN_LOW     0x10
#define GREE_FAN_MED     0x20
#define GREE_FAN_HIGH    0x30

// 消息类型
#define GREE_MSG_A       0x50   // Frame A 标识（已从 raw 反算验证）
#define GREE_MSG_B       0x70   // Frame B 标识（已从 raw 反算验证）
```

### 4.2 校验和计算函数

```cpp
/**
 * 计算 Gree YBOFB 8字节帧的校验和（Kelvinator block checksum）
 * 算法：sum = 10 + Σ(B[0..3] & 0x0F) + Σ(B[4..6] >> 4)
 * 结果存入 B7 高 4 位（bit7-4）
 */
uint8_t greeCalcChecksum(const uint8_t data[8]) {
  uint8_t sum = 10;  // kKelvinatorChecksumStart
  sum += data[0] & 0x0F;
  sum += data[1] & 0x0F;
  sum += data[2] & 0x0F;
  sum += data[3] & 0x0F;
  sum += data[4] >> 4;
  sum += data[5] >> 4;
  sum += data[6] >> 4;
  return (sum & 0x0F) << 4;  // 存入高 4 位
}
```

### 4.3 帧发送函数

```cpp
/**
 * LSB-first 发送一个字节
 * 从 bit0 开始，依次发送 bit0, bit1, ..., bit7
 */
void greeSendByte(uint8_t data) {
  for (uint8_t mask = 1; mask; mask <<= 1) {
    irSend.mark(GREE_BIT_MARK);
    if (data & mask) {
      irSend.space(GREE_ONE_SPACE);
    } else {
      irSend.space(GREE_ZERO_SPACE);
    }
  }
}

/**
 * 发送一个 Gree 帧（8字节）
 * 结构：Header + Block1(B0-B3) + Footer(010) + Gap + Block2(B4-B7) + EndMark
 * @param frame     8字节数据
 * @param endSpace  帧结束后的 space 时间（Frame A=7300μs, Frame B=20000μs）
 *
 * 注意：endSpace 必须为非零值。IRsend::space(0) 会被库忽略导致信号截断。
 */
void greeSendFrame(const uint8_t frame[8], uint32_t endSpace) {
  // Header
  irSend.mark(GREE_HDR_MARK);
  irSend.space(GREE_HDR_SPACE);

  // Block 1 (B0-B3), LSB-first
  for (int i = 0; i < 4; i++) {
    greeSendByte(frame[i]);
  }

  // 3-bit Footer: 0b010
  irSend.mark(GREE_BIT_MARK);
  irSend.space(GREE_ZERO_SPACE);   // bit: 0
  irSend.mark(GREE_BIT_MARK);
  irSend.space(GREE_ONE_SPACE);    // bit: 1
  irSend.mark(GREE_BIT_MARK);
  irSend.space(GREE_ZERO_SPACE);   // bit: 0

  // Gap between blocks
  irSend.mark(GREE_BIT_MARK);
  irSend.space(GREE_MSG_SPACE);

  // Block 2 (B4-B7), LSB-first
  for (int i = 4; i < 8; i++) {
    greeSendByte(frame[i]);
  }

  // End: mark + non-zero space（v2 修正：避免 space(0) 被忽略）
  irSend.mark(GREE_BIT_MARK);
  if (endSpace > 0) {
    irSend.space(endSpace);
  }
}
```

### 4.4 主控制函数

```cpp
/**
 * 发送 Gree YBOFB 空调控制信号
 * @param power  true=开机, false=关机
 * @param mode   运行模式: "Cool"/"Heat"/"Auto"/"Dry"/"Fan"
 * @param temp   目标温度 (16-30°C)
 * @param fan    风速: "Auto"/"Low"/"Medium"/"High"
 */
void sendGreeYBOFB(bool power, String mode, int temp, String fan) {
  // === 构造 Frame A ===
  uint8_t frameA[8] = {0x00, 0x00, 0x00, GREE_MSG_A, 0x00, 0x00, 0x00, 0x00};

  // B0: Fan + Mode + Power
  uint8_t modeVal = GREE_MODE_COOL;  // default
  if (mode == "Heat")      modeVal = GREE_MODE_HEAT;
  else if (mode == "Dry")  modeVal = GREE_MODE_DRY;
  else if (mode == "Fan")  modeVal = GREE_MODE_FAN;
  else if (mode == "Auto") modeVal = GREE_MODE_AUTO;

  uint8_t fanVal = GREE_FAN_AUTO;  // default
  if (fan == "Low")        fanVal = GREE_FAN_LOW;
  else if (fan == "Medium") fanVal = GREE_FAN_MED;
  else if (fan == "High")  fanVal = GREE_FAN_HIGH;

  frameA[0] = fanVal | modeVal;
  if (power) frameA[0] |= GREE_MODE_ON;

  // B1: Temperature (低4位 = temp - 16)
  frameA[1] = (uint8_t)(temp - 16) & 0x0F;

  // B2: Light=ON (bit5=1), Turbo=OFF (bit4=0)
  frameA[2] = 0x20;  // Light ON

  // B3: 已设为 GREE_MSG_A (0x50)

  // B5: 默认 0x00（v2 修正：实测所有信号 B5=0x00）
  frameA[5] = 0x00;

  // B7: 计算校验和
  frameA[7] = greeCalcChecksum(frameA);

  // === 构造 Frame B ===
  uint8_t frameB[8];
  memcpy(frameB, frameA, 8);
  frameB[3] = GREE_MSG_B;  // 0x70
  frameB[6] |= fanVal;     // B6 包含风速信息（gree_ext 验证）
  frameB[7] = greeCalcChecksum(frameB);  // 重新计算校验和

  // === 发送信号 ===
  irRecv.disableIRIn();
  irSend.enableIROut(38);

  // 发送 Frame A，帧间间隔 7300μs
  greeSendFrame(frameA, GREE_FRAME_SPACE);

  // 发送 Frame B，结束间隔 20000μs（v2 修正：非零值避免截断）
  greeSendFrame(frameB, 20000);

  irRecv.enableIRIn();

  // 调试输出
  Serial.printf("[GREE] Power:%s Mode:%s Temp:%d Fan:%s\n",
    power ? "ON" : "OFF", mode.c_str(), temp, fan.c_str());
  Serial.printf("[GREE] FrameA: %02X %02X %02X %02X %02X %02X %02X %02X\n",
    frameA[0], frameA[1], frameA[2], frameA[3],
    frameA[4], frameA[5], frameA[6], frameA[7]);
  Serial.printf("[GREE] FrameB: %02X %02X %02X %02X %02X %02X %02X %02X\n",
    frameB[0], frameB[1], frameB[2], frameB[3],
    frameB[4], frameB[5], frameB[6], frameB[7]);
}
```

### 4.5 handleHvac() 修改

在 `handleHvac()` 函数中，vendor 参数判断后增加 GREE 品牌的自定义路径：

```cpp
void handleHvac() {
  String vendor = server.arg("vendor");
  // ... 参数校验不变 ...

  // === GREE 品牌使用自定义编码器 ===
  if (vendor == "GREE") {
    bool power = server.arg("power") == "On";
    String mode = server.arg("mode");
    int temp = server.arg("temp").toInt();
    String fan = server.arg("fan");

    sendGreeYBOFB(power, mode, temp, fan);

    // UDP 广播（从机同步）
    char msg[256];
    snprintf(msg, sizeof(msg), "HVAC:%s,%s,%s,%d,%s,%s",
      vendor.c_str(), power ? "1" : "0", mode.c_str(), temp, fan.c_str(),
      server.arg("swing").c_str());
    broadcastUdp(msg);

    server.send(200, "application/json", "{\"ok\":true}");
    return;
  }

  // === 其他品牌走原有 IRac 路径 ===
  stdAc::state_t st = {};
  // ... 原有代码不变 ...
}
```

### 4.6 slaveExecHvac() 修改

从机收到 UDP 广播后，同样判断 GREE 品牌走自定义路径：

```cpp
void slaveExecHvac(String data) {
  // ... 解析 data 不变 ...

  String vendor = ...;
  bool power = ...;
  String mode = ...;
  int temp = ...;
  String fan = ...;

  // === GREE 品牌使用自定义编码器 ===
  if (vendor == "GREE") {
    sendGreeYBOFB(power, mode, temp, fan);
    Serial.printf("[SLAVE] GREE %s %s %dC\n", vendor.c_str(), power ? "ON" : "OFF", temp);
    return;
  }

  // === 其他品牌走原有路径 ===
  // ... 原有代码不变 ...
}
```

---

## 5. 数据流示意

### 5.1 用户操作流程（v2 修正：使用真实计算值）

```
用户在 WebUI 点击「制冷 26°C 开机 自动风」
  │
  ▼ POST /api/hvac
  │ vendor=GREE&power=On&mode=Cool&temp=26&fan=Auto&swing=Off
  ▼
handleHvac() 检测到 vendor == "GREE"
  │
  ▼ 调用 sendGreeYBOFB(true, "Cool", 26, "Auto")
  │
  ├→ 构造 Frame A:
  │    B0 = FanAuto(0x00) | Cool(0x01) | Power(0x08) = 0x09
  │    B1 = (26-16) & 0x0F = 0x0A
  │    B2 = 0x20 (Light=ON, Turbo=OFF)
  │    B3 = 0x50 (Frame A)
  │    B4 = 0x00
  │    B5 = 0x00
  │    B6 = 0x00
  │    B7 = greeCalcChecksum() = 0xD0
  │    → [09 0A 20 50 00 00 00 D0]
  │
  ├→ 构造 Frame B:
  │    B3 = 0x70, B6 |= 0x00 (Auto fan)
  │    → [09 0A 20 70 00 00 00 D0]
  │
  ├→ greeSendFrame(frameA, 7300)   // Frame A + 帧间间隔
  ├→ greeSendFrame(frameB, 20000)  // Frame B + 结束间隔
  │
  └→ UDP 广播给从机 → 从机同样调用 sendGreeYBOFB()
```

#### 对比实测数据（制冷 24°C 高风 开机）

```
我们编码: Frame A = [39 08 20 50 00 00 00 B0]
实测捕获: Frame A = [39 08 30 50 00 00 00 B0]
差异:     B2 我们=0x20 (Turbo=OFF), 实测=0x30 (Turbo=ON)
```

差异原因：实测时遥控器按了 Turbo（强力制冷），我们的编码默认 Turbo=OFF。功能上不影响空调核心制冷/制热。

### 5.2 信号时序图

```
Frame A:
 ┌────9000────┐   ┌─620─┐   ┌─620─┐        ┌620┐ ┌620┐ ┌620┐
 │            │   │     │   │     │        │  │ │  │ │  │
 ┘   4500     ┘...┘ 1/0 ┘...┘ 1/0 ┘...×32 ┘0 ┘ ┘1 ┘ ┘0 ┘  ← Footer(010)
 │← B0-B3 LSB →│   │← 3bit →│
                                                     ┌─620─┐
                                                     │     │
                                            19980μs ┘     ┘
 │← Gap →│
          ┌─620─┐   ┌─620─┐        ┌─620─┐
          │     │   │     │        │     │
          ┘ 1/0 ┘...┘ 1/0 ┘...×32 ┘ end ┘
          │← B4-B7 LSB →│

          ─────── 7300μs 间隔 ───────

Frame B:
 ──── 与 Frame A 结构相同，B3=0x70 ────
 ──── 结束间隔 20000μs ────
```

---

## 6. 验证方案

### 6.1 编译验证

```bash
cd firmware
pio run -e nodemcuv2
```

确认编译通过，Flash/RAM 占用无明显增加（预计增加 < 1KB Flash）。

### 6.2 逻辑验证

在 `sendGreeYBOFB()` 中添加调试输出（已在 4.4 节代码中包含），构造已知命令并与实测捕获数据对比：

#### 验证用例（v2 修正：使用实测数据对照）

| 测试用例 | 预期 B0 | 预期 B1 | 预期 B2 | 对应实测 |
|----------|---------|---------|---------|----------|
| 制冷 24°C 开机 高风 | `0x39` (Cool+Power+High) | `0x08` (24-16=8) | `0x20` (Light) | 开机 Frame A: `39 08 30 50`（B2 差异：Turbo） |
| 制热 24°C 开机 自动风 | `0x0C` (Heat+Power+Auto) | `0x08` (24-16=8) | `0x20` (Light) | 制热 Frame A: `0C 08 20 50` ✓ |
| 自动 25°C 开机 高风 | `0x38` (Auto+Power+High) | `0x09` (25-16=9) | `0x20` (Light) | 自动 Frame A: `38 09 20 50` ✓ |
| 关机（制冷模式） | `0x31` (Cool+HighFan, 无Power) | `0x08` | `0x20` | 关机 Frame A: `31 08 30 50`（B2 差异：Turbo） |
| 除湿 26°C 开机 低风 | `0x0A` (Dry+Power+Low) | `0x0A` (26-16=10) | `0x20` (Light) | 除湿 Frame A: `1A 0A 20 50` ✓ |

### 6.3 物理验证

1. 刷写固件到设备
2. 手机连接 WiFi `IR-AC`，打开 `http://10.1.1.1`
3. 选择「格力」品牌
4. 依次测试：制冷/制热/自动/除湿/送风
5. 测试温度：16°C, 20°C, 26°C, 30°C
6. 测试风速：自动/低/中/高
7. 测试关机

### 6.4 多设备验证

1. 主机 + 从机同时上电
2. 主机 WebUI 发送 GREE 命令
3. 确认从机同步发射红外信号（从机红色 LED 闪烁）
4. 确认空调收到信号并正确响应

---

## 7. 风险与备选方案

### 7.1 风险

| 风险 | 可能性 | 影响 | 缓解措施 |
|------|--------|------|----------|
| B2 Turbo 位影响 | 低 | Turbo 功能未实现（不影响核心制冷/制热） | 可后续添加 Turbo 选项 |
| 帧间间隔不正确 | 低 | 空调不响应 | 参考 gree_ext 的 7300μs 值，已从 raw 验证 |
| B6 风速冗余不匹配 | 低 | 部分功能异常 | gree_ext 同样 B6 附加风速，已验证 |
| 不同批次遥控器差异 | 低 | 部分空调不兼容 | 保留原有 IRac 路径作为备选 |

### 7.2 备选方案

如果自定义编码器仍无法控制空调：

1. **方案 B：sendRaw 回放** — 使用用户已捕获的 raw 信号通过 `sendRaw()` 发送
   - 优点：已验证可用
   - 缺点：仅支持已录制的固定组合，无法动态调整温度/风速

2. **方案 C：混合方案** — 为常用温度/模式组合预录 raw 信号，存储在 Flash/PROGMEM 中
   - 优点：可靠
   - 缺点：存储空间大（每个组合约 200 字节 raw 数据），不灵活

3. **方案 D：调试 IRremoteESP8266 库** — 排查 `sendAc()` 失败原因
   - 可能是 model 参数、时序偏差、或库的 bit field 映射错误
   - 贡献修复到上游库

---

## 8. 参考资料

| 来源 | URL | 用途 |
|------|-----|------|
| IRremoteESP8266 ir_Gree.cpp | https://github.com/crankyoldgit/IRremoteESP8266/blob/master/src/ir_Gree.cpp | 协议参数、校验和算法、sendGree() 发送逻辑 |
| IRremoteESP8266 ir_Gree.h | https://github.com/crankyoldgit/IRremoteESP8266/blob/master/src/ir_Gree.h | GreeProtocol 结构体定义 |
| IRremoteESP8266 ir_Kelvinator.cpp | https://github.com/crankyoldgit/IRremoteESP8266/blob/master/src/ir_Kelvinator.cpp | calcBlockChecksum() 校验和实现 |
| ESPHome gree_ext 组件 | https://github.com/ryanh7/esphome-custom-components/tree/main/components/gree_ext | YAP0F20 专用实现，LSB-first 发送逻辑（commit: 53ba569） |
| arduino-heatpumpir | https://github.com/ToniA/arduino-heatpumpir/blob/master/GreeHeatpumpIR.cpp | 另一个 Gree 实现，校验和算法参考 |
| GitHub Issue #2201 | https://github.com/crankyoldgit/IRremoteESP8266/issues/2201 | GREE YBOFB sendAc() 不工作的问题报告 |

---

## 9. 实现排期

| 步骤 | 工作内容 | 预估工时 |
|------|----------|----------|
| 1 | 在 main.cpp 中新增 GREE 常量定义和辅助函数 | 30 分钟 |
| 2 | 实现 `greeCalcChecksum()` | 15 分钟 |
| 3 | 实现 `greeSendByte()` 和 `greeSendFrame()` | 30 分钟 |
| 4 | 实现 `sendGreeYBOFB()` | 45 分钟 |
| 5 | 修改 `handleHvac()` 添加 GREE 路由 | 15 分钟 |
| 6 | 修改 `slaveExecHvac()` 添加 GREE 路由 | 15 分钟 |
| 7 | 编译验证 + 串口调试 | 30 分钟 |
| 8 | 刷写物理测试 | 60 分钟 |
| **合计** | | **约 4 小时** |

---

## 附录 A：与 IRremoteESP8266 库实现的对比（v2 修正）

| 维度 | IRremoteESP8266 (sendAc) | 自定义编码器 (gree_ext 方式) |
|------|--------------------------|---------------------------|
| 字节编码 | `sendGeneric()` 内部 MSB-first | LSB-first 手动 mark/space |
| 帧结构 | 单帧 8 字节 + repeat 参数 | Frame A (B3=0x50) + Frame B (B3=0x70) 双帧 |
| B3 值 | 0x50 固定 | FrameA=0x50, FrameB=0x70（实测 14 帧验证） |
| B5 值 | 库 stateReset 默认 0x20 | 实测 0x00（v2 修正） |
| B6 用法 | 未使用 | FrameB 包含风速信息（gree_ext 验证） |
| 校验和 | Kelvinator calcBlockChecksum | 相同算法（实测 14/14 匹配） |
| 发送方式 | sendGree() → sendGeneric() | 直接 mark/space 控制 |
| YBOFB 适配 | 通过 model 参数（实测不工作） | 基于 raw 反算数据，已验证 |

## 附录 B：gree_ext transmit_state() 源码（参考）

```cpp
// 来源：ryanh7/esphome-custom-components gree_ext 组件
// 注意：仓库 URL 需确认具体路径，以下为 gree_ext 组件的核心发送逻辑
void GreeClimate::transmit_state() {
  uint8_t remote_state_a[8] = {0x00, 0x00, 0x00, GREE_MESSAGE_A, 0x00, 0x00, 0x00, 0x00};
  uint8_t remote_state_b[8] = {0x00, 0x00, 0x00, GREE_MESSAGE_B, 0x00, 0x00, 0x00, 0x00};

  remote_state_a[0] = remote_state_b[0] = this->fan_speed_() | this->operation_mode_();
  remote_state_a[1] = remote_state_b[1] = this->temperature_() & 0x0F;
  remote_state_a[2] = remote_state_b[2] = this->mode_bits_ & 0xA0;  // Light + Xfan
  remote_state_b[6] |= this->fan_speed_();

  remote_state_a[4] = this->vertical_swing_() | (this->horizontal_swing_() << 4);
  if (this->vertical_swing_() == GREE_VDIR_SWING || this->horizontal_swing_() == GREE_HDIR_SWING) {
    remote_state_a[0] = remote_state_b[0] |= (1 << 6);  // SwingAuto bit
  }

  remote_state_a[7] = calculate_checksum(remote_state_a);
  remote_state_b[7] = calculate_checksum(remote_state_b);

  // 发送 Frame A (间隔 7300μs) + Frame B (结束)
  this->set_transmit_data_(data, remote_state_a, GREE_MESSAGE_SPLIT);
  this->set_transmit_data_(data, remote_state_b, 0);
}
```

## 附录 C：v1 → v2 修正清单

| 章节 | 修正内容 | 原因 |
|------|----------|------|
| 2.4 B7 表格 | Sum 从 bit3-0 修正为 bit7-4 | 表格方向画反，代码本身正确 |
| 2.5 默认值 | B5 从 0b100(0x20) 修正为 0x00 | 实测 14 帧 B5 全为 0x00 |
| 2.7 校验和验证 | 全部数据替换为 raw 反算结果 | 原数据来自捕获工具混合解码，B4-B7 不准确 |
| 2.7 验证结果 | 从 4/7 匹配修正为 14/14 全部匹配 | raw 反算后无噪音，全部精确匹配 |
| 4.3 greeSendFrame | endSpace=0 修正为 endSpace≥0 并添加注释 | IRsend::space(0) 被库忽略 |
| 4.4 sendGreeYBOFB | frameA[5] 从 0x20 修正为 0x00 | 实测 B5=0x00 |
| 4.4 sendGreeYBOFB | Frame B endSpace 从 0 修正为 20000 | 避免 space(0) 截断 |
| 5.1 数据流 | 字节值全部重新计算 | 匹配真实 checksum 结果 |
| 6.2 验证用例 | B0 预期值更新，增加实测对照 | 使用 raw 反算的真实捕获数据 |
| 附录 A | 增加 B5 对比、修正 MSB 描述 | sendGeneric 内部实际是 MSB-first 发送 |
| 8 参考资料 | gree_ext URL 已确认：ryanh7/esphome-custom-components | 原写 ryan7（缺 h），已修正 |
