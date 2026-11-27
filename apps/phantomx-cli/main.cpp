#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

#include "phantomx/common/cli.hpp"
#include "phantomx/common/config.hpp"
#include "phantomx/common/logging.hpp"
#include "phantomx/common/paths.hpp"
#include "phantomx/rpc/rpc.hpp"

namespace {
constexpr const char* kName = "phantomx-cli";
constexpr const char* kVersion = "0.1.0";
}  // namespace

int main(int argc, char* argv[]) {
  phantomx::CliParser parser(kName);
  parser.add_option("--config", "Path to config file");
  parser.add_option("--datadir", "Data directory (default: network datadir)");
  parser.add_option("--rpc", "RPC endpoint (foundation: in-process dispatch)", "http://127.0.0.1:8741");
  parser.add_option("--rpc-token", "RPC authentication token");
  parser.add_option("--rpc-user", "RPC user name", "local");
  parser.add_flag("--testnet", "Use testnet");
  parser.add_flag("--regtest", "Use regtest");
  auto parsed = parser.parse(argc, argv);
  if (!parsed.ok()) {
    std::cerr << parsed.error().to_string() << "\n" << parser.help_text();
    return 2;
  }
  auto args = parsed.value();
  auto has_flag = [&](const char* f) { return std::find(args.flags.begin(), args.flags.end(), f) != args.flags.end(); };
  auto opt = [&](const char* k) -> std::optional<std::string> {
    auto it = args.options.find(k);
    if (it == args.options.end() || it->second.empty()) return std::nullopt;
    return it->second;
  };

  const bool cli_testnet = has_flag("--testnet");
  const bool cli_regtest = has_flag("--regtest");
  std::filesystem::path bootstrap_config = opt("--config").has_value()
      ? std::filesystem::path(*opt("--config"))
      : phantomx::default_config_path(phantomx::default_datadir(phantomx::Network::kMainnet));
  std::optional<std::string> cfg_testnet, cfg_regtest, cfg_chain;
  if (std::filesystem::exists(bootstrap_config)) {
    auto boot_cfg = phantomx::Config::load_file(bootstrap_config);
    if (boot_cfg.ok()) {
      cfg_testnet = boot_cfg.value().get("testnet");
      cfg_regtest = boot_cfg.value().get("regtest");
      cfg_chain = boot_cfg.value().get("chain");
    }
  }
  auto network_r = phantomx::resolve_network(cli_testnet, cli_regtest, cfg_testnet, cfg_regtest, cfg_chain);
  if (!network_r.ok()) { std::cerr << network_r.error().to_string() << "\n"; return 2; }
  const auto network = network_r.value();

  if (has_flag("--help")) {
    std::cout << parser.help_text();
    std::cout << "\nRPC methods are accessed as: phantomx-cli <method> [--key value ...]\n";
    return 0;
  }
  if (has_flag("--version")) {
    std::cout << kName << " " << kVersion << " (" << phantomx::network_name(network) << ")\n";
    return 0;
  }

  const std::filesystem::path datadir = opt("--datadir").has_value() ? std::filesystem::path(*opt("--datadir")) : phantomx::default_datadir(network);
  const std::filesystem::path config_path = opt("--config").has_value() ? std::filesystem::path(*opt("--config")) : phantomx::default_config_path(datadir);
  phantomx::Config cfg;
  if (std::filesystem::exists(config_path)) {
    auto loaded = phantomx::Config::load_file(config_path);
    if (loaded.ok()) cfg = std::move(loaded.value());
  }

  auto logger = phantomx::Logger(kName, std::clog);
  auto log_level_it = args.options.find("--log-level");
  if (log_level_it != args.options.end()) logger.set_level(phantomx::parse_log_level(log_level_it->second));
  else logger.set_level(phantomx::parse_log_level(cfg.get_or("loglevel", "info")));

  const auto rpc_port = cfg.get_or("rpcport", std::to_string(phantomx::default_rpc_port(network)));
  const auto rpc_host = cfg.get_or("rpcbind", "127.0.0.1");
  const auto default_rpc = "http://" + (rpc_host == "0.0.0.0" ? std::string("127.0.0.1") : rpc_host) + ":" + rpc_port;
  if (!opt("--rpc").has_value()) args.options["--rpc"] = default_rpc;
  const std::string user = opt("--rpc-user").value_or("local");
  const std::string token = opt("--rpc-token").value_or(cfg.get_or("rpctoken", cfg.get_or("rpcpassword", "local-dev-token")));
  args.options["--rpc-token"] = token;

  phantomx::rpc::RpcServer server;
  phantomx::rpc::register_default_methods(server);
  server.add_user({user, token, {}});
  server.set_rate_limit(100, 60);

  if (args.command.empty()) {
    logger.info("rpc_methods", {{"count", std::to_string(server.methods().size())}, {"network", std::string(phantomx::network_name(network))}});
    for (const auto& m : server.methods()) std::cout << m << "\n";
    return 0;
  }

  phantomx::rpc::RpcRequest req;
  req.id = "cli-1";
  req.method = args.command;
  req.auth_token = token;
  for (const auto& [k, v] : args.options) {
    if (k == "--rpc" || k == "--rpc-token" || k == "--rpc-user" || k == "--config" || k == "--log-level" ||
        k == "--datadir" || k == "--testnet" || k == "--regtest") continue;
    req.params[k] = v;
  }

  auto resp = server.call(req, 0);
  std::cout << phantomx::rpc::rpc_response_to_json(resp) << "\n";
  return resp.ok ? 0 : 1;
}
