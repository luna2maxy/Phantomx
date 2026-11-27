#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "phantomx/common/error.hpp"

namespace phantomx {

class Config {
 public:
  [[nodiscard]] static Result<Config> load_file(const std::filesystem::path& path);

  void set(std::string key, std::string value);
  [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
  [[nodiscard]] std::string get_or(std::string_view key, std::string_view default_value) const;
  [[nodiscard]] bool has(std::string_view key) const;
  [[nodiscard]] const std::unordered_map<std::string, std::string>& values() const noexcept {
    return values_;
  }

 private:
  std::unordered_map<std::string, std::string> values_;
};

}  // namespace phantomx
