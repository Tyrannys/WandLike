#pragma once
// Tiny value-or-error type used across the core (no exceptions for expected failures such as "process exited").
#include <string>
#include <utility>
#include <variant>

namespace hx {

struct Error {
    std::string message;
    unsigned long code = 0;   // Win32 error code when relevant
};

template <class T>
class Result {
public:
    Result(T value) : v_(std::move(value)) {}            // NOLINT: implicit on purpose
    Result(Error e) : v_(std::move(e)) {}                // NOLINT
    [[nodiscard]] bool ok() const { return v_.index() == 0; }
    explicit operator bool() const { return ok(); }
    T& value() { return std::get<0>(v_); }
    const T& value() const { return std::get<0>(v_); }
    T& operator*() { return value(); }
    const T& operator*() const { return value(); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    [[nodiscard]] const Error& error() const { return std::get<1>(v_); }
    [[nodiscard]] const std::string& message() const { return error().message; }

private:
    std::variant<T, Error> v_;
};

// Result<void> stand-in.
struct Ok {};
using Status = Result<Ok>;
inline Status ok_status() { return Ok{}; }

}  // namespace hx
