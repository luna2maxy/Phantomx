#include "phantomx/common/paths.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace phantomx {
namespace {

std::string lower_ascii(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

bool is_truthy(std::string_view value) {
  const auto v = lower_ascii(value);
  return v == "1" || v == "true" || v == "yes" || v == "on";
}

[[maybe_unused]] bool is_falsey(std::string_view value) {
  const auto v = lower_ascii(value);
  return v == "0" || v == "false" || v == "no" || v == "off" || v.empty();
}

inline const char* safe_getenv(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
  return value;
}

std::optional<std::filesystem::path> home_dir() {
#ifdef _WIN32
  const char* appdata = safe_getenv("APPDATA");
  if (appdata != nullptr && *appdata != '\0') return std::filesystem::path(appdata);
  const char* userprofile = safe_getenv("USERPROFILE");
  if (userprofile != nullptr && *userprofile != '\0') return std::filesystem::path(userprofile) / "AppData" / "Roaming";
  return std::nullopt;
#else
  const char* home = safe_getenv("HOME");
  if (home != nullptr && *home != '\0') return std::filesystem::path(home);
  return std::nullopt;
#endif
}

}  // namespace

// network_name(Network) is provided by blockchain/genesis.

std::optional<Network> network_from_string(std::string_view value) noexcept {
  const auto v = lower_ascii(value);
  if (v == "main" || v == "mainnet") return Network::kMainnet;
  if (v == "test" || v == "testnet") return Network::kTestnet;
  if (v == "regtest") return Network::kRegtest;
  return std::nullopt;
}

std::filesystem::path default_data_root() {
#ifdef _WIN32
  auto base = home_dir();
  if (base.has_value()) return *base / "PhantomX";
  return std::filesystem::current_path() / "PhantomX";
#else
  auto base = home_dir();
  if (base.has_value()) return *base / ".phantomx";
  return std::filesystem::current_path() / ".phantomx";
#endif
}

std::filesystem::path default_datadir(Network network) {
  const auto root = default_data_root();
  switch (network) {
    case Network::kMainnet: return root;
    case Network::kTestnet: return root / "testnet";
    case Network::kRegtest: return root / "regtest";
  }
  return root;
}

std::filesystem::path default_config_path(const std::filesystem::path& datadir) {
  return datadir / "phantomx.conf";
}

Result<Network> resolve_network(bool testnet_flag, bool regtest_flag,
                                std::optional<std::string_view> testnet_cfg,
                                std::optional<std::string_view> regtest_cfg,
                                std::optional<std::string_view> chain_cfg) {
  const bool cfg_testnet = testnet_cfg.has_value() && is_truthy(*testnet_cfg);
  const bool cfg_regtest = regtest_cfg.has_value() && is_truthy(*regtest_cfg);

  if (testnet_flag && regtest_flag) return MakeError(ErrorCode::kConfigError, "testnet and regtest cannot both be enabled");
  if (cfg_testnet && cfg_regtest) return MakeError(ErrorCode::kConfigError, "config: testnet=1 and regtest=1 cannot both be enabled");

  if (regtest_flag) return Network::kRegtest;
  if (testnet_flag) return Network::kTestnet;
  if (cfg_regtest) return Network::kRegtest;
  if (cfg_testnet) return Network::kTestnet;

  if (chain_cfg.has_value()) {
    auto n = network_from_string(*chain_cfg);
    if (!n.has_value()) return MakeError(ErrorCode::kConfigError, "invalid chain value: " + std::string(*chain_cfg));
    return *n;
  }
  return Network::kMainnet;
}

std::uint16_t default_rpc_port(Network network) noexcept {
  switch (network) {
    case Network::kMainnet: return 8741;
    case Network::kTestnet: return 18741;
    case Network::kRegtest: return 18841;
  }
  return 8741;
}

std::uint16_t default_p2p_port(Network network) noexcept {
  switch (network) {
    case Network::kMainnet: return 18444;
    case Network::kTestnet: return 28444;
    case Network::kRegtest: return 38444;
  }
  return 18444;
}

std::string generate_rpc_secret() {
  static constexpr char kHex[] = "0123456789abcdef";
  std::random_device rd;
  std::uniform_int_distribution<int> dist(0, 15);
  std::string out;
  out.resize(32);
  for (char& c : out) c = kHex[dist(rd)];
  return out;
}

std::string render_default_config_template(const ConfigTemplateOptions& options) {
  std::ostringstream oss;
  oss << "# PhantomX configuration\n";
  oss << "# This file is created automatically. All options are commented out (#) so they are not active by default.\n";
  oss << "# Remove the # at the beginning of a line to enable that option.\n\n";
  oss << "# =========================\n";
  oss << "# Network\n";
  oss << "# =========================\n";
  oss << "# Choose one. Default: mainnet.\n";
  oss << (options.network == Network::kTestnet ? "#testnet=1\n" : "#testnet=1\n");
  oss << (options.network == Network::kRegtest ? "#regtest=1\n" : "#regtest=1\n");
  oss << "\n";
  oss << "# =========================\n";
  oss << "# Service\n";
  oss << "# =========================\n";
  oss << "#server=1\n";
  oss << "#listen=1\n\n";
  oss << "# =========================\n";
  oss << "# RPC\n";
  oss << "# =========================\n";
  oss << "#rpcbind=" << options.rpcbind << "\n";
  oss << "#rpcport=" << options.rpcport << "\n";
  oss << "#rpcallowip=" << options.rpcallowip << "\n";
  oss << "#rpcuser=" << options.rpcuser << "\n";
  oss << "#rpcpassword=" << (options.rpcpassword.empty() ? generate_rpc_secret() : options.rpcpassword) << "\n";
  oss << "#rpctoken=" << (options.rpctoken.empty() ? generate_rpc_secret() : options.rpctoken) << "\n\n";
  oss << "# =========================\n";
  oss << "# P2P\n";
  oss << "# =========================\n";
  oss << "#port=" << options.p2pport << "\n\n";
  oss << "# =========================\n";
  oss << "# Logging\n";
  oss << "# =========================\n";
  oss << "#loglevel=info\n";
  return oss.str();
}

Result<bool> ensure_default_config(const std::filesystem::path& datadir, const ConfigTemplateOptions& options) {
  std::error_code ec;
  std::filesystem::create_directories(datadir, ec);
  if (ec) return MakeError(ErrorCode::kIoError, "failed to create datadir: " + datadir.string() + ": " + ec.message());

  const auto cfg_path = default_config_path(datadir);
  if (std::filesystem::exists(cfg_path, ec)) return false;
  if (ec) return MakeError(ErrorCode::kIoError, "failed to stat config: " + cfg_path.string() + ": " + ec.message());

  std::ofstream out(cfg_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) return MakeError(ErrorCode::kIoError, "failed to create config: " + cfg_path.string());
  const auto content = render_default_config_template(options);
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  out.close();
  if (!out) return MakeError(ErrorCode::kIoError, "failed to write config: " + cfg_path.string());

#ifndef _WIN32
  std::filesystem::permissions(cfg_path,
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, ec);
  // ignore permission errors; config was created
#endif
  return true;
}

}  // namespace phantomx
