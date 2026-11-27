#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

#include "phantomx/common/cli.hpp"
#include "phantomx/common/config.hpp"
#include "phantomx/common/logging.hpp"
#include "phantomx/common/types.hpp"
#include "phantomx/wallet/keystore.hpp"

namespace {
constexpr const char* kName = "phantomx-wallet";
constexpr const char* kVersion = "0.1.0";
}  // namespace

int main(int argc, char* argv[]) {
  phantomx::CliParser parser(kName);
  parser.add_option("--datadir", "Wallet data directory", "./phantomx-wallet-data");
  parser.add_option("--rpc", "PhantomX node RPC endpoint", "http://127.0.0.1:8740");
  parser.add_option("--keystore", "Keystore file path", "./phantomx-wallet-data/wallet.keystore");
  auto parsed = parser.parse(argc, argv);
  if (!parsed.ok()) {
    std::cerr << parsed.error().to_string() << "\n" << parser.help_text();
    return 2;
  }
  const auto& args = parsed.value();
  if (std::find(args.flags.begin(), args.flags.end(), "--help") != args.flags.end()) {
    std::cout << parser.help_text();
    std::cout << "\nCommands: create | import | backup | restore | lock | unlock | balance | send | receive | history\n";
    return 0;
  }
  if (std::find(args.flags.begin(), args.flags.end(), "--version") != args.flags.end()) {
    std::cout << kName << " " << kVersion << "\n";
    return 0;
  }

  auto logger = phantomx::Logger(kName, std::clog);
  auto log_level_it = args.options.find("--log-level");
  if (log_level_it != args.options.end()) logger.set_level(phantomx::parse_log_level(log_level_it->second));

  const auto datadir = args.options.count("--datadir") ? args.options.at("--datadir") : "./phantomx-wallet-data";
  const auto rpc = args.options.count("--rpc") ? args.options.at("--rpc") : "http://127.0.0.1:8740";
  const auto keystore_path = args.options.count("--keystore") ? args.options.at("--keystore") : (datadir + "/wallet.keystore");

  logger.info("startup", {phantomx::Logger::Field{"app", kName}, phantomx::Logger::Field{"version", kVersion}, phantomx::Logger::Field{"rpc", rpc}});
  logger.info("security", {phantomx::Logger::Field{"seed_phrase_sent_to_server", "false"}, phantomx::Logger::Field{"private_key_local_only", "true"}});

  if (args.command == "create") {
    phantomx::wallet::Keystore ks;
    auto g = ks.generate(phantomx::crypto::KeyRole::kWalletSigning);
    if (!g.ok()) {
      logger.error("wallet_create_failed", {phantomx::Logger::Field{"error", g.error().to_string()}});
      return 1;
    }
    std::filesystem::create_directories(std::filesystem::path(keystore_path).parent_path());
    auto s = ks.save(keystore_path);
    if (!s.ok()) {
      logger.error("wallet_save_failed", {phantomx::Logger::Field{"error", s.error().to_string()}});
      return 1;
    }
    logger.info("wallet_created", {phantomx::Logger::Field{"keystore", keystore_path}});
    return 0;
  }
  if (args.command == "restore" || args.command == "import") {
    logger.info("wallet_action", {phantomx::Logger::Field{"action", args.command}, phantomx::Logger::Field{"status", "requires_local_keystore_or_seed_input"}});
    return 0;
  }

  logger.info("command", {phantomx::Logger::Field{"name", args.command.empty() ? "none" : args.command}, phantomx::Logger::Field{"status", "foundation_requires_node_rpc_integration"}});
  return 0;
}
