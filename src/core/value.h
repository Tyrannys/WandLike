#pragma once
// Typed values used by the scanner, address list and trainers.
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace hx {

enum class ValueType { U8, I16, I32, I64, F32, F64 };

size_t type_size(ValueType t);
const char* type_name(ValueType t);                         // "byte", "int16", "int32", "int64", "float", "double"
std::optional<ValueType> parse_type(std::string_view name); // also accepts uint8/int8/i32/f32/...
bool is_float(ValueType t);

struct Value {
    ValueType type = ValueType::I32;
    uint64_t bits = 0;                                      // raw little-endian bytes, zero extended

    static std::optional<Value> parse(ValueType t, std::string_view text);   // decimal, 0x-hex for integers, "1.5" for floats
    static Value from_bytes(ValueType t, const void* bytes);
    template <class T> static Value of(ValueType t, T v) {
        Value r;
        r.type = t;
        r.bits = 0;
        std::memcpy(&r.bits, &v, sizeof(T) < 8 ? sizeof(T) : 8);
        return r;
    }
    static Value from_double(ValueType t, double d);        // converts (rounds for integer types)
    static Value from_i64(ValueType t, int64_t i);

    void to_bytes(void* out) const;                         // writes type_size(type) bytes
    [[nodiscard]] double as_double() const;
    [[nodiscard]] int64_t as_i64() const;                   // floats are truncated
    [[nodiscard]] std::string format() const;               // "100", "-3", "1.5"
    bool operator==(const Value& o) const { return type == o.type && bits == o.bits; }
};

}  // namespace hx
