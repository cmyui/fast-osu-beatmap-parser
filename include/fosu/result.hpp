#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace fosu {

enum class ErrorCode : uint8_t {
    InvalidInput,
    IoFailure,
    InputTooLarge,
    AllocationFailure,
};

inline constexpr size_t kNoErrorOffset = static_cast<size_t>(-1);

struct Error {
    ErrorCode code;
    size_t input_offset = kNoErrorOffset;
    int os_code = 0;
};

template <typename T>
class Result {
public:
    Result(T value) noexcept : value_(std::move(value)), succeeded_(true) {}
    Result(Error error) noexcept : error_(error), succeeded_(false) {}

    explicit operator bool() const noexcept { return succeeded_; }

    T& value() & noexcept {
        assert(succeeded_);
        return value_;
    }

    const T& value() const& noexcept {
        assert(succeeded_);
        return value_;
    }

    T&& value() && noexcept {
        assert(succeeded_);
        return std::move(value_);
    }

    const Error& error() const noexcept {
        assert(!succeeded_);
        return error_;
    }

private:
    T value_{};
    Error error_{ErrorCode::InvalidInput};
    bool succeeded_;
};

}  // namespace fosu
