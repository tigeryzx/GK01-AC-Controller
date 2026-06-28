#include "core/json_builder.h"

JsonBuilder::JsonBuilder(char* buf, size_t size)
    : buf_(buf), size_(size) {
    if (size_ > 0) buf_[0] = '\0';
}

void JsonBuilder::ensure(size_t additional) {
    if (pos_ + additional + 1 >= size_) overflowed_ = true;
}

void JsonBuilder::put(char c) {
    if (overflowed_) return;
    if (pos_ + 1 >= size_) { overflowed_ = true; return; }
    buf_[pos_++] = c;
}

void JsonBuilder::putRaw(const char* s) {
    while (*s) put(*s++);
}

void JsonBuilder::putEscaped(const char* s) {
    put('"');
    for (; *s; ++s) {
        char c = *s;
        if (c == '"' || c == '\\') { put('\\'); put(c); }
        else if ((uint8_t)c < 0x20) { put('\\'); put('u'); put('0'); put('0');
                                       put((c < 0x10) ? '0' : '1');
                                       put((c & 0x0F) < 10 ? '0' + (c & 0x0F) : 'a' + (c & 0x0F) - 10); }
        else put(c);
    }
    put('"');
}

void JsonBuilder::putCommaIfNeed() {
    if (needComma_) put(',');
    else needComma_ = true;
}

void JsonBuilder::beginObject() { put('{'); needComma_ = false; }
void JsonBuilder::endObject()   { put('}'); needComma_ = true; }
void JsonBuilder::beginArray()  { put('['); needComma_ = false; }
void JsonBuilder::endArray()    { put(']'); needComma_ = true; }

void JsonBuilder::kvString(const char* key, const char* v) {
    putCommaIfNeed(); put('"'); putRaw(key); put('"'); put(':'); putEscaped(v);
}
void JsonBuilder::kvString(const char* key, const String& v) { kvString(key, v.c_str()); }

void JsonBuilder::kvInt(const char* key, long v) {
    putCommaIfNeed(); put('"'); putRaw(key); put('"'); put(':');
    char tmp[16]; snprintf(tmp, sizeof(tmp), "%ld", v); putRaw(tmp);
}

void JsonBuilder::kvUInt(const char* key, unsigned long v) {
    putCommaIfNeed(); put('"'); putRaw(key); put('"'); put(':');
    char tmp[16]; snprintf(tmp, sizeof(tmp), "%lu", v); putRaw(tmp);
}

void JsonBuilder::kvBool(const char* key, bool v) {
    putCommaIfNeed(); put('"'); putRaw(key); put('"'); put(':');
    putRaw(v ? "true" : "false");
}

void JsonBuilder::kvNull(const char* key) {
    putCommaIfNeed(); put('"'); putRaw(key); put('"'); put(':'); putRaw("null");
}

void JsonBuilder::kvRaw(const char* key, const char* rawJsonFragment) {
    putCommaIfNeed(); put('"'); putRaw(key); put('"'); put(':'); putRaw(rawJsonFragment);
}

void JsonBuilder::appendString(const char* v) { putCommaIfNeed(); putEscaped(v); }
void JsonBuilder::appendString(const String& v) { appendString(v.c_str()); }
void JsonBuilder::appendInt(long v) {
    putCommaIfNeed(); char tmp[16]; snprintf(tmp, sizeof(tmp), "%ld", v); putRaw(tmp);
}
void JsonBuilder::appendBool(bool v) { putCommaIfNeed(); putRaw(v ? "true" : "false"); }
void JsonBuilder::appendRaw(const char* rawJsonFragment) { putCommaIfNeed(); putRaw(rawJsonFragment); }
