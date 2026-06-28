#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace str {

void copyTo(char* dst, size_t dstSize, const String& value);

String sanitizeConfigValue(String value, size_t maxLen);
String sanitizeMetadata(String value, size_t maxLen);
String sanitizeIconKey(String value);

bool isValidMacString(const String& mac);
bool parseMacBytes(const char* mac, uint8_t out[6]);
bool parseMacBytes(const String& mac, uint8_t out[6]);
int  hexNibble(char c);

int parseRawTimings(const String& s, uint16_t* buf, int maxLen);

String jsonEscape(const String& s);

String slaveIdFromMac(const char* mac);
String slaveIdFromMac(const String& mac);

}
