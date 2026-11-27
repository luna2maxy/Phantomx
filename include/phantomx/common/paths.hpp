#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/common/error.hpp"

namespace phantomx {
[[nodiscard]] std::optional<Network> network_from_string(std::string_view value) noexcept;

struct NetworkSelection {
  Network network = Network::kMainnet;
  bool explicit_selection = false;
};

struct ConfigTemplateOptions {
  Network network = Network::kMainnet;
  std::string rpcbind = "0.0.0.0";
  std::string rpcallowip = "127.0.0.1";
  std::string rpcuser = "phantomxrpc";
  std::string rpcpassword;
  std::string rpctoken;
  std::uint16_t rpcport = 8741;
  std::uint16_t p2pport = 18444;
};

[[nodiscard]] std::filesystem::path default_data_root();
[[nodiscard]] std::filesystem::path default_datadir(Network network);
[[nodiscard]] std::filesystem::path default_config_path(const std::filesystem::path& datadir);

[[nodiscard]] Result<Network> resolve_network(bool testnet_flag, bool regtest_flag,
                                              std::optional<std::string_view> testnet_cfg,
                                              std::optional<std::string_view> regtest_cfg,
                                              std::optional<std::string_view> chain_cfg);
[[nodiscard]] std::uint16_t default_rpc_port(Network network) noexcept;
[[nodiscard]] std::uint16_t default_p2p_port(Network network) noexcept;

[[nodiscard]] std::string generate_rpc_secret();
[[nodiscard]] std::string render_default_config_template(const ConfigTemplateOptions& options);
[[nodiscard]] Result<bool> ensure_default_config(const std::filesystem::path& datadir,
                                                 const ConfigTemplateOptions& options);

}  // namespace phantomx
