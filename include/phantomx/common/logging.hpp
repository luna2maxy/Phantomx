#pragma once

#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>

namespace phantomx {

enum class LogLevel : std::uint8_t {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
  kCritical = 5,
  kOff = 6,
};

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;
[[nodiscard]] LogLevel parse_log_level(std::string_view text, LogLevel fallback = LogLevel::kInfo);

class Logger {
 public:
  struct Field {
    std::string key;
    std::string value;
  };

  explicit Logger(std::string component = "phantomx", std::ostream& out = std::clog);

  void set_level(LogLevel level) noexcept;
  [[nodiscard]] LogLevel level() const noexcept;

  void log(LogLevel level, std::string_view message,
           std::initializer_list<Field> fields = {}) const;

  void trace(std::string_view message, std::initializer_list<Field> fields = {}) const;
  void debug(std::string_view message, std::initializer_list<Field> fields = {}) const;
  void info(std::string_view message, std::initializer_list<Field> fields = {}) const;
  void warn(std::string_view message, std::initializer_list<Field> fields = {}) const;
  void error(std::string_view message, std::initializer_list<Field> fields = {}) const;
  void critical(std::string_view message, std::initializer_list<Field> fields = {}) const;

 private:
  [[nodiscard]] static std::string timestamp_utc();

  std::string component_;
  std::ostream& out_;
  LogLevel level_{LogLevel::kInfo};
  mutable std::mutex mutex_;
};

Logger& default_logger();

}  // namespace phantomx
