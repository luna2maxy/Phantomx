#include <algorithm>
#include <iostream>
#include <string>

#include "phantomx/common/cli.hpp"
#include "phantomx/common/config.hpp"
#include "phantomx/common/logging.hpp"

namespace {
constexpr const char* kName = "phantomx-pool";
constexpr const char* kVersion = "0.1.0";
}  // namespace

int main(int argc, char* argv[]) {
  phantomx::CliParser parser(kName);
  parser.add_option("--listen", "Pool listen address", "0.0.0.0:8750");
  parser.add_option("--api-listen", "Pool API listen address", "127.0.0.1:8751");
  auto parsed = parser.parse(argc, argv);
  if (!parsed.ok()) {
    std::cerr << parsed.error().to_string() << "\n" << parser.help_text();
    return 2;
  }
  const auto& args = parsed.value();
  if (std::find(args.flags.begin(), args.flags.end(), "--help") != args.flags.end()) {
    std::cout << parser.help_text();
    return 0;
  }
  if (std::find(args.flags.begin(), args.flags.end(), "--version") != args.flags.end()) {
    std::cout << kName << " " << kVersion << "\n";
    return 0;
  }
  auto logger = phantomx::Logger(kName, std::clog);
  auto log_level_it = args.options.find("--log-level");
  if (log_level_it != args.options.end()) logger.set_level(phantomx::parse_log_level(log_level_it->second));
  if (auto cfg_it = args.options.find("--config"); cfg_it != args.options.end() && !cfg_it->second.empty()) {
    auto cfg = phantomx::Config::load_file(cfg_it->second);
    if (!cfg.ok()) {
      logger.error("config_load_failed", {{"error", cfg.error().to_string()}});
      return 1;
    }
  }
  auto listen_it = args.options.find("--listen");
  auto api_it = args.options.find("--api-listen");
  logger.info("startup", {{"app", kName}, {"version", kVersion},
                          {"listen", listen_it != args.options.end() ? listen_it->second : ""},
                          {"api_listen", api_it != args.options.end() ? api_it->second : ""}});
  logger.info("pool_roles", {{"roles", "operator,poi_worker,verifier_worker"}});
  return 0;
}
