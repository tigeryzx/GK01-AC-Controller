#include "hw/hvac/wahin_encoder.h"
#include "hw/ir_service.h"

namespace {
inline constexpr uint16_t WAHIN_HDR_MARK    = 4380;
inline constexpr uint16_t WAHIN_HDR_SPACE   = 4420;
inline constexpr uint16_t WAHIN_BIT_MARK    = 460;
inline constexpr uint16_t WAHIN_ONE_SPACE   = 1640;
inline constexpr uint16_t WAHIN_ZERO_SPACE  = 620;
inline constexpr uint16_t WAHIN_FRAME_GAP   = 5230;

const uint8_t WAHIN_TEMP_GRAY[] = {
    0x0, 0x1, 0x3, 0x2, 0x6, 0x7, 0x5, 0x4,
    0xC, 0xD, 0x9, 0x8, 0xA, 0xB
};

void sendByte(IRsend& s, uint8_t data) {
    for (uint8_t mask = 0x80; mask; mask >>= 1) {
        s.mark(WAHIN_BIT_MARK);
        s.space((data & mask) ? WAHIN_ONE_SPACE : WAHIN_ZERO_SPACE);
    }
}

void sendFrame(IRsend& s, const uint8_t data[3]) {
    s.mark(WAHIN_HDR_MARK);
    s.space(WAHIN_HDR_SPACE);
    for (int i = 0; i < 3; i++) {
        sendByte(s, data[i]);
        sendByte(s, (uint8_t)~data[i]);
    }
    s.mark(WAHIN_BIT_MARK);
    s.space(WAHIN_FRAME_GAP);
}
}

bool WahinEncoder::send(const hvac::Command& cmd) {
    int temp = constrain(cmd.temp, minTemp(), maxTemp());

    uint8_t data[3] = { 0xB2, 0x00, 0x00 };

    uint8_t fanBits = 0xB;
    if      (cmd.fan == "Low")                              fanBits = 0x9;
    else if (cmd.fan == "Medium")                           fanBits = 0x5;
    else if (cmd.fan == "High" || cmd.fan == "Max" ||
             cmd.fan == "Highest")                          fanBits = 0x3;
    data[1] = (uint8_t)((fanBits << 4) | (cmd.power ? 0xF : 0xB));

    uint8_t modeBits = 0x0;
    if      (cmd.mode == "Heat") modeBits = 0x3;
    else if (cmd.mode == "Fan")  modeBits = 0x1;
    else if (cmd.mode == "Auto") modeBits = 0x2;
    else if (cmd.mode == "Dry")  modeBits = 0x2;
    uint8_t tempGray = WAHIN_TEMP_GRAY[temp - 17];
    data[2] = (uint8_t)((tempGray << 4) | (modeBits << 2));

    ir.disableRx();
    IRsend& s = ir.rawSender();
    s.enableIROut(38);
    sendFrame(s, data);
    sendFrame(s, data);
    ir.enableRx();

    Serial.printf("[WAHIN] Pwr:%s Mode:%s T:%d Fan:%s Data=[%02X %02X %02X]\n",
                  cmd.power ? "ON" : "OFF", cmd.mode.c_str(), temp, cmd.fan.c_str(),
                  data[0], data[1], data[2]);
    return true;
}
