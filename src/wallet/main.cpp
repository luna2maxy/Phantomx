#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

#include "phantomx/common/cli.hpp"
#include "phantomx/common/config.hpp"
#include "phantomx/common/logging.hpp"
#include "phantomx/common/paths.hpp"
#include "phantomx/common/types.hpp"

namespace {
constexpr const char* kName = "phantomx-wallet";
constexpr const char* kVersion = "0.1.0";
}  // namespace

int main(int argc, char* argv[]) {
  phantomx::CliParser parser(kName);
  parser.add_option("--datadir", "Wallet data directory (default: network datadir/wallet)");
  parser.add_flag("--testnet", "Use testnet");
  parser.add_flag("--regtest", "Use regtest");
  auto parsed = parser.parse(argc, argv);
  if (!parsed.ok()) {
    std::cerr << parsed.error().to_string() << "\n" << parser.help_text();
    return 2;
  }
  const auto& args = parsed.value();
  auto has_flag = [&](const char* f) { return std::find(args.flags.begin(), args.flags.end(), f) != args.flags.end(); };
  if (has_flag("--help")) { std::cout << parser.help_text(); return 0; }

  std::optional<std::string> t = has_flag("--testnet") ? std::optional<std::string>("1") : std::nullopt;
  std::optional<std::string> r = has_flag("--regtest") ? std::optional<std::string>("1") : std::nullopt;
  auto network_r = phantomx::resolve_network(has_flag("--testnet"), has_flag("--regtest"), std::nullopt, std::nullopt, std::nullopt);
  if (!network_r.ok()) { std::cerr << network_r.error().to_string() << "\n"; return 2; }
  const auto network = network_r.value();

  if (has_flag("--version")) { std::cout << kName << " " << kVersion << " (" << phantomx::network_name(network) << ")\n"; return 0; }

  auto logger = phantomx::Logger(kName, std::clog);
  auto log_level_it = args.options.find("--log-level");
  if (log_level_it != args.options.end()) logger.set_level(phantomx::parse_log_level(log_level_it->second));
  if (auto cfg_it = args.options.find("--config"); cfg_it != args.options.end() && !cfg_it->second.empty()) {
    auto cfg = phantomx::Config::load_file(cfg_it->second);
    if (!cfg.ok()) { logger.error("config_load_failed", {{"error", cfg.error().to_string()}}); return 1; }
  }

  auto datadir_it = args.options.find("--datadir");
  const auto datadir = datadir_it != args.options.end() ? datadir_it->second : (phantomx::default_datadir(network) / "wallet").string();
  logger.info("startup", {{"app", kName}, {"version", kVersion}, {"network", std::string(phantomx::network_name(network))}, {"datadir", datadir}, {"address_prefix", std::string(phantomx::kAddressPrefix)}});
  return 0;
}
