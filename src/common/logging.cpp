#include "phantomx/common/logging.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace phantomx {

std::string_view to_string(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::kTrace: return "TRACE";
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo: return "INFO";
    case LogLevel::kWarn: return "WARN";
    case LogLevel::kError: return "ERROR";
    case LogLevel::kCritical: return "CRITICAL";
    case LogLevel::kOff: return "OFF";
  }
  return "UNKNOWN";
}

LogLevel parse_log_level(std::string_view text, LogLevel fallback) {
  std::string upper(text);
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  if (upper == "TRACE") return LogLevel::kTrace;
  if (upper == "DEBUG") return LogLevel::kDebug;
  if (upper == "INFO") return LogLevel::kInfo;
  if (upper == "WARN" || upper == "WARNING") return LogLevel::kWarn;
  if (upper == "ERROR") return LogLevel::kError;
  if (upper == "CRITICAL") return LogLevel::kCritical;
  if (upper == "OFF") return LogLevel::kOff;
  return fallback;
}

Logger::Logger(std::string component, std::ostream& out)
    : component_(std::move(component)), out_(out) {}

void Logger::set_level(LogLevel level) noexcept { level_ = level; }
LogLevel Logger::level() const noexcept { return level_; }

std::string Logger::timestamp_utc() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto time = system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count() << 'Z';
  return oss.str();
}

void Logger::log(LogLevel level, std::string_view message,
                 std::initializer_list<Field> fields) const {
  if (level < level_ || level_ == LogLevel::kOff) return;
  std::ostringstream oss;
  oss << "{\"ts\":\"" << timestamp_utc() << "\",\"level\":\"" << to_string(level)
      << "\",\"component\":\"" << component_ << "\",\"msg\":\"";
  for (char c : message) {
    switch (c) {
      case '\\': oss << "\\\\"; break;
      case '"': oss << "\\\""; break;
      case '\n': oss << "\\n"; break;
      case '\r': oss << "\\r"; break;
      case '\t': oss << "\\t"; break;
      default: oss << c; break;
    }
  }
  oss << '"';
  for (const auto& field : fields) {
    oss << ",\"" << field.key << "\":\"" << field.value << '"';
  }
  oss << '}';
  std::lock_guard<std::mutex> lock(mutex_);
  out_ << oss.str() << std::endl;
}

void Logger::trace(std::string_view message, std::initializer_list<Field> fields) const { log(LogLevel::kTrace, message, fields); }
void Logger::debug(std::string_view message, std::initializer_list<Field> fields) const { log(LogLevel::kDebug, message, fields); }
void Logger::info(std::string_view message, std::initializer_list<Field> fields) const { log(LogLevel::kInfo, message, fields); }
void Logger::warn(std::string_view message, std::initializer_list<Field> fields) const { log(LogLevel::kWarn, message, fields); }
void Logger::error(std::string_view message, std::initializer_list<Field> fields) const { log(LogLevel::kError, message, fields); }
void Logger::critical(std::string_view message, std::initializer_list<Field> fields) const { log(LogLevel::kCritical, message, fields); }

Logger& default_logger() {
  static Logger logger("phantomx", std::clog);
  return logger;
}

}  // namespace phantomx
