#include "phantomx/common/cli.hpp"

#include <sstream>

namespace phantomx {

CliParser::CliParser(std::string program_name) : program_name_(std::move(program_name)) {
  add_flag("--help", "Show help");
  add_flag("--version", "Show version");
  add_option("--config", "Path to config file", "");
  add_option("--log-level", "Log level (trace,debug,info,warn,error,critical)", "info");
}

void CliParser::add_option(std::string name, std::string description, std::string default_value) {
  specs_.push_back({std::move(name), std::move(description), std::move(default_value), false});
}

void CliParser::add_flag(std::string name, std::string description) {
  specs_.push_back({std::move(name), std::move(description), "", true});
}

Result<ParsedArgs> CliParser::parse(int argc, char* argv[]) const {
  ParsedArgs out;
  // defaults
  for (const auto& spec : specs_) {
    if (!spec.is_flag && !spec.default_value.empty()) {
      out.options[spec.name] = spec.default_value;
    }
  }

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i] ? argv[i] : "";
    if (arg.empty()) continue;
    if (arg.rfind("--", 0) == 0) {
      const auto eq = arg.find('=');
      const std::string name = eq == std::string::npos ? arg : arg.substr(0, eq);
      const OptionSpec* spec = nullptr;
      for (const auto& s : specs_) if (s.name == name) { spec = &s; break; }
      if (spec == nullptr) {
        return MakeError(ErrorCode::kInvalidArgument, "unknown option: " + name);
      }
      if (spec->is_flag) {
        out.flags.push_back(name);
        continue;
      }
      std::string value;
      if (eq != std::string::npos) {
        value = arg.substr(eq + 1);
      } else {
        if (i + 1 >= argc) return MakeError(ErrorCode::kInvalidArgument, "missing value for " + name);
        ++i;
        value = argv[i] ? argv[i] : "";
      }
      out.options[name] = value;
    } else {
      if (out.command.empty()) out.command = arg;
      else out.positionals.push_back(arg);
    }
  }
  return out;
}

std::string CliParser::help_text() const {
  std::ostringstream oss;
  oss << "Usage: " << program_name_ << " [command] [options]\n\nOptions:\n";
  for (const auto& spec : specs_) {
    oss << "  " << spec.name;
    if (!spec.is_flag) oss << " <value>";
    oss << "\n      " << spec.description;
    if (!spec.is_flag && !spec.default_value.empty()) oss << " (default: " << spec.default_value << ")";
    oss << "\n";
  }
  return oss.str();
}

}  // namespace phantomx
