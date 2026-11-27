#include "phantomx/common/config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <jansson.h>

namespace {
void flatten_json(json_t* value, const std::string& prefix, phantomx::Config& cfg) {
  if (json_is_object(value)) {
    const char* key = nullptr;
    json_t* child = nullptr;
    json_object_foreach(value, key, child) {
      const auto path = prefix.empty() ? std::string(key) : prefix + "." + key;
      flatten_json(child, path, cfg);
    }
  } else if (json_is_array(value)) {
    char* text = json_dumps(value, JSON_COMPACT);
    cfg.set(prefix, text != nullptr ? text : "[]");
    if (text != nullptr) free(text);
  } else if (json_is_string(value)) {
    cfg.set(prefix, json_string_value(value));
  } else if (json_is_true(value)) {
    cfg.set(prefix, "true");
  } else if (json_is_false(value)) {
    cfg.set(prefix, "false");
  } else if (json_is_integer(value)) {
    cfg.set(prefix, std::to_string(json_integer_value(value)));
  } else if (json_is_real(value)) {
    cfg.set(prefix, std::to_string(json_real_value(value)));
  } else if (json_is_null(value)) {
    cfg.set(prefix, "null");
  }
}
}  // namespace

namespace phantomx {


Result<Config> Config::load_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in.is_open()) return MakeError(ErrorCode::kIoError, "unable to open config file: " + path.string());
  std::stringstream buffer;
  buffer << in.rdbuf();
  json_error_t error{};
  json_t* root = json_loads(buffer.str().c_str(), 0, &error);
  if (root != nullptr) {
    if (!json_is_object(root)) { json_decref(root); return MakeError(ErrorCode::kConfigError, "config root must be a JSON object"); }
    Config cfg;
    flatten_json(root, "", cfg);
    json_decref(root);
    return cfg;
  }
  // Preserve compatibility with legacy key=value fixtures.
  Config cfg;
  std::string line;
  std::size_t line_no = 0;
  std::istringstream legacy(buffer.str());
  while (std::getline(legacy, line)) {
    ++line_no;
    const auto comment = line.find('#');
    if (comment != std::string::npos) line.resize(comment);
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) continue;
    const auto last = line.find_last_not_of(" \t\r\n");
    line = line.substr(first, last - first + 1);
    if (!line.empty() && line.front() == '[' && line.back() == ']') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) return MakeError(ErrorCode::kConfigError, "invalid config syntax at line " + std::to_string(line_no));
    const auto key_end = line.find_last_not_of(" \t", eq - 1);
    const auto value_start = line.find_first_not_of(" \t", eq + 1);
    if (key_end == std::string::npos || value_start == std::string::npos) return MakeError(ErrorCode::kConfigError, "invalid config syntax at line " + std::to_string(line_no));
    cfg.set(line.substr(first, key_end - first + 1), line.substr(value_start));
  }
  return cfg;
}

void Config::set(std::string key, std::string value) { values_[std::move(key)] = std::move(value); }

std::optional<std::string> Config::get(std::string_view key) const {
  auto it = values_.find(std::string(key));
  if (it == values_.end()) return std::nullopt;
  return it->second;
}

std::string Config::get_or(std::string_view key, std::string_view default_value) const {
  auto value = get(key);
  return value ? *value : std::string(default_value);
}

bool Config::has(std::string_view key) const { return values_.find(std::string(key)) != values_.end(); }

}  // namespace phantomx
