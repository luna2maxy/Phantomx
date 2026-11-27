#include "phantomx/common/error.hpp"

namespace phantomx {

std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kOk: return "ok";
    case ErrorCode::kInvalidArgument: return "invalid_argument";
    case ErrorCode::kNotFound: return "not_found";
    case ErrorCode::kParseError: return "parse_error";
    case ErrorCode::kConfigError: return "config_error";
    case ErrorCode::kIoError: return "io_error";
    case ErrorCode::kSerializationError: return "serialization_error";
    case ErrorCode::kInternalError: return "internal_error";
    case ErrorCode::kUnauthorized: return "unauthorized";
    case ErrorCode::kForbidden: return "forbidden";
  }
  return "unknown_error";
}

std::string Error::to_string() const {
  if (ok()) return "ok";
  std::string out = std::string(phantomx::to_string(code_));
  if (!message_.empty()) {
    out += ": ";
    out += message_;
  }
  return out;
}

}  // namespace phantomx
