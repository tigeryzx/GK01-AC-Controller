#include "core/string_utils.h"

namespace str {

void copyTo(char* dst, size_t dstSize, const String& value) {
    if (!dst || dstSize == 0) return;
    strncpy(dst, value.c_str(), dstSize - 1);
    dst[dstSize - 1] = '\0';
}

String sanitizeConfigValue(String value, size_t maxLen) {
    String out;
    out.reserve(min(value.length(), maxLen));
    for (unsigned int i = 0; i < value.length() && out.length() < maxLen; i++) {
        char c = value.charAt(i);
        if (c == '\r' || c == '\n') c = ' ';
        if ((uint8_t)c < 0x20) continue;
        out += c;
    }
    return out;
}

String sanitizeMetadata(String value, size_t maxLen) {
    value.trim();
    String out;
    out.reserve(min(value.length(), maxLen));
    for (unsigned int i = 0; i < value.length() && out.length() < maxLen; i++) {
        char c = value.charAt(i);
        if (c == '|' || c == ':' || c == '\r' || c == '\n' ||
            c == '<' || c == '>' || c == '&' || c == '"' ||
            c == '\'' || c == '`') {
            c = ' ';
        }
        if ((uint8_t)c < 0x20) continue;
        out += c;
    }
    out.trim();
    return out;
}

String sanitizeIconKey(String value) {
    value.trim();
    String out;
    out.reserve(11);
    for (unsigned int i = 0; i < value.length() && out.length() < 11; i++) {
        char c = value.charAt(i);
        if (isalnum((unsigned char)c) || c == '_' || c == '-') out += c;
    }
    if (out.length() == 0) out = "ac";
    return out;
}

bool isValidMacString(const String& mac) {
    if (mac.length() != 17) return false;
    for (int i = 0; i < 17; i++) {
        if (i == 2 || i == 5 || i == 8 || i == 11 || i == 14) {
            if (mac.charAt(i) != ':') return false;
        } else if (!isxdigit((unsigned char)mac.charAt(i))) {
            return false;
        }
    }
    return true;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseMacBytes(const char* mac, uint8_t out[6]) {
    if (!mac || strlen(mac) != 17) return false;
    for (int i = 0; i < 6; i++) {
        int pos = i * 3;
        int hi = hexNibble(mac[pos]);
        int lo = hexNibble(mac[pos + 1]);
        if (hi < 0 || lo < 0) return false;
        if (i < 5 && mac[pos + 2] != ':') return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

bool parseMacBytes(const String& mac, uint8_t out[6]) {
    return parseMacBytes(mac.c_str(), out);
}

int parseRawTimings(const String& s, uint16_t* buf, int maxLen) {
    int count = 0, start = 0;
    while (count < maxLen) {
        int comma = s.indexOf(',', start);
        String part = (comma >= 0) ? s.substring(start, comma) : s.substring(start);
        part.trim();
        if (part.length() == 0) break;
        buf[count++] = (uint16_t)part.toInt();
        if (comma < 0) break;
        start = comma + 1;
    }
    return count;
}

String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (unsigned int i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

String slaveIdFromMac(const char* mac) {
    String id = "";
    id.reserve(4);
    for (int i = 12; i < 17; i += 3) {
        if (mac[i]) id += (char)toupper(mac[i]);
        if (mac[i + 1]) id += (char)toupper(mac[i + 1]);
    }
    return id;
}

String slaveIdFromMac(const String& mac) {
    return slaveIdFromMac(mac.c_str());
}

}
