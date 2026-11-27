#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phantomx/common/error.hpp"

namespace phantomx {

struct ParsedArgs {
  std::string command;
  std::unordered_map<std::string, std::string> options;
  std::vector<std::string> flags;
  std::vector<std::string> positionals;
};

class CliParser {
 public:
  explicit CliParser(std::string program_name);

  void add_option(std::string name, std::string description, std::string default_value = "");
  void add_flag(std::string name, std::string description);

  [[nodiscard]] Result<ParsedArgs> parse(int argc, char* argv[]) const;
  [[nodiscard]] std::string help_text() const;

 private:
  struct OptionSpec {
    std::string name;
    std::string description;
    std::string default_value;
    bool is_flag = false;
  };

  std::string program_name_;
  std::vector<OptionSpec> specs_;
};

}  // namespace phantomx
