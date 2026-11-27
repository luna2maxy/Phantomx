#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace phantomx {

enum class ErrorCode {
  kOk = 0,
  kInvalidArgument,
  kNotFound,
  kParseError,
  kConfigError,
  kIoError,
  kSerializationError,
  kInternalError,
  kUnauthorized,
  kForbidden,
};

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

class Error {
 public:
  Error() = default;
  Error(ErrorCode code, std::string message) : code_(code), message_(std::move(message)) {}

  [[nodiscard]] ErrorCode code() const noexcept { return code_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] bool ok() const noexcept { return code_ == ErrorCode::kOk; }

  [[nodiscard]] std::string to_string() const;

 private:
  ErrorCode code_{ErrorCode::kOk};
  std::string message_;
};

template <typename T>
class Result {
 public:
  Result(T value) : value_(std::move(value)), error_() {}
  Result(Error error) : value_(), error_(std::move(error)) {}

  [[nodiscard]] bool ok() const noexcept { return error_.ok(); }
  [[nodiscard]] const Error& error() const noexcept { return error_; }
  [[nodiscard]] const T& value() const& noexcept { return value_; }
  [[nodiscard]] T& value() & noexcept { return value_; }
  [[nodiscard]] T&& value() && noexcept { return std::move(value_); }

 private:
  T value_{};
  Error error_;
};

template <>
class Result<void> {
 public:
  Result() : error_() {}
  Result(Error error) : error_(std::move(error)) {}

  [[nodiscard]] bool ok() const noexcept { return error_.ok(); }
  [[nodiscard]] const Error& error() const noexcept { return error_; }

 private:
  Error error_;
};

inline Error Ok() { return {}; }
inline Error MakeError(ErrorCode code, std::string message) { return Error(code, std::move(message)); }

}  // namespace phantomx
