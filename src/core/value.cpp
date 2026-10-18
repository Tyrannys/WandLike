#include "core/value.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/util.h"

namespace hx {

size_t type_size(ValueType t) {
    switch (t) {
        case ValueType::U8: return 1;
        case ValueType::I16: return 2;
        case ValueType::I32: case ValueType::F32: return 4;
        default: return 8;
    }
}

const char* type_name(ValueType t) {
    switch (t) {
        case ValueType::U8: return "byte";
        case ValueType::I16: return "int16";
        case ValueType::I32: return "int32";
        case ValueType::I64: return "int64";
        case ValueType::F32: return "float";
        default: return "double";
    }
}

std::optional<ValueType> parse_type(std::string_view name) {
    std::string n = lower(name);
    if (n == "byte" || n == "uint8" || n == "int8" || n == "u8" || n == "i8") return ValueType::U8;
    if (n == "int16" || n == "uint16" || n == "i16" || n == "short") return ValueType::I16;
    if (n == "int32" || n == "uint32" || n == "i32" || n == "int" || n == "4 bytes") return ValueType::I32;
    if (n == "int64" || n == "uint64" || n == "i64" || n == "long" || n == "8 bytes") return ValueType::I64;
    if (n == "float" || n == "f32" || n == "single") return ValueType::F32;
    if (n == "double" || n == "f64") return ValueType::F64;
    return std::nullopt;
}

bool is_float(ValueType t) { return t == ValueType::F32 || t == ValueType::F64; }

Value Value::from_bytes(ValueType t, const void* bytes) {
    Value v;
    v.type = t;
    v.bits = 0;
    std::memcpy(&v.bits, bytes, type_size(t));
    return v;
}

void Value::to_bytes(void* out) const { std::memcpy(out, &bits, type_size(type)); }

double Value::as_double() const {
    switch (type) {
        case ValueType::U8: return (double)(uint8_t)bits;
        case ValueType::I16: return (double)(int16_t)bits;
        case ValueType::I32: return (double)(int32_t)bits;
        case ValueType::I64: return (double)(int64_t)bits;
        case ValueType::F32: { float f; std::memcpy(&f, &bits, 4); return (double)f; }
        default: { double d; std::memcpy(&d, &bits, 8); return d; }
    }
}

int64_t Value::as_i64() const {
    switch (type) {
        case ValueType::U8: return (uint8_t)bits;
        case ValueType::I16: return (int16_t)bits;
        case ValueType::I32: return (int32_t)bits;
        case ValueType::I64: return (int64_t)bits;
        default: return (int64_t)as_double();
    }
}

Value Value::from_double(ValueType t, double d) {
    switch (t) {
        case ValueType::F32: return of<float>(t, (float)d);
        case ValueType::F64: return of<double>(t, d);
        default: return from_i64(t, (int64_t)std::llround(d));
    }
}

Value Value::from_i64(ValueType t, int64_t i) {
    switch (t) {
        case ValueType::U8: return of<uint8_t>(t, (uint8_t)i);
        case ValueType::I16: return of<int16_t>(t, (int16_t)i);
        case ValueType::I32: return of<int32_t>(t, (int32_t)i);
        case ValueType::I64: return of<int64_t>(t, i);
        case ValueType::F32: return of<float>(t, (float)i);
        default: return of<double>(t, (double)i);
    }
}

std::optional<Value> Value::parse(ValueType t, std::string_view text) {
    std::string s = trim(text);
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    errno = 0;
    if (is_float(t)) {
        double d = std::strtod(s.c_str(), &end);
        if (end == s.c_str() || *end != '\0' || errno == ERANGE) return std::nullopt;
        return from_double(t, d);
    }
    bool neg = s[0] == '-';
    std::string body = (neg || s[0] == '+') ? s.substr(1) : s;
    int base = 10;
    if (body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) { base = 16; body = body.substr(2); }
    if (body.empty()) return std::nullopt;
    unsigned long long u = std::strtoull(body.c_str(), &end, base);
    if (*end != '\0' || errno == ERANGE) return std::nullopt;
    int64_t i = neg ? -(int64_t)u : (int64_t)u;
    // reject out-of-range literals instead of wrapping silently
    switch (t) {
        case ValueType::U8: if (i < -128 || i > 255) return std::nullopt; break;
        case ValueType::I16: if (i < -32768 || i > 65535) return std::nullopt; break;
        case ValueType::I32: if (i < INT32_MIN || (u > 0xFFFFFFFFull && !neg)) return std::nullopt; break;
        default: break;
    }
    return from_i64(t, i);
}

std::string Value::format() const {
    char buf[64];
    switch (type) {
        case ValueType::F32: std::snprintf(buf, sizeof buf, "%.7g", as_double()); return buf;
        case ValueType::F64: std::snprintf(buf, sizeof buf, "%.15g", as_double()); return buf;
        default: std::snprintf(buf, sizeof buf, "%lld", (long long)as_i64()); return buf;
    }
}

}  // namespace hx
