#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// 栈分配 JSON 构造器，零堆分配，替代 String += 拼接
class JsonBuilder {
public:
    JsonBuilder(char* buf, size_t size);

    void beginObject();
    void endObject();
    void beginArray();
    void endArray();

    void kvString(const char* key, const char* value);
    void kvString(const char* key, const String& value);
    void kvInt(const char* key, long value);
    void kvUInt(const char* key, unsigned long value);
    void kvBool(const char* key, bool value);
    void kvNull(const char* key);
    void kvRaw(const char* key, const char* rawJsonFragment);

    void appendString(const char* value);
    void appendString(const String& value);
    void appendInt(long value);
    void appendBool(bool value);
    void appendRaw(const char* rawJsonFragment);

    const char* result() const { return buf_; }
    size_t length() const { return pos_; }
    bool   overflowed() const { return overflowed_; }

private:
    char*  buf_;
    size_t size_;
    size_t pos_        = 0;
    bool   overflowed_ = false;
    bool   needComma_  = false;

    void ensure(size_t additional);
    void putCommaIfNeed();
    void put(char c);
    void putRaw(const char* s);
    void putRaw_P(PGM_P s);
    void putEscaped(const char* s);
};
