#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phantomx/common/crypto.hpp"
#include "phantomx/common/error.hpp"

namespace phantomx::wallet {

struct WalletAccount {
  std::string label;
  crypto::KeyPair keypair;
};

class Keystore {
 public:
  [[nodiscard]] static bool valid_label(const std::string& label);
  [[nodiscard]] Result<void> generate(crypto::KeyRole role);
  [[nodiscard]] Result<void> import_secret_hex(crypto::KeyRole role, std::string_view secret_hex);
  [[nodiscard]] bool has(crypto::KeyRole role) const;
  [[nodiscard]] std::optional<crypto::KeyPair> get(crypto::KeyRole role) const;
  [[nodiscard]] Result<void> generate_account(const std::string& label);
  [[nodiscard]] Result<void> import_account(const std::string& label, std::string_view secret_hex);
  [[nodiscard]] bool has_account(const std::string& label) const;
  [[nodiscard]] std::optional<crypto::KeyPair> get_account(const std::string& label) const;
  [[nodiscard]] std::vector<std::string> account_labels() const;
  [[nodiscard]] std::vector<WalletAccount> accounts() const;

  // Saves public metadata and secret keys to a local file. File should be protected (0600 on Unix).
  [[nodiscard]] Result<void> save(const std::filesystem::path& path) const;
  [[nodiscard]] static Result<Keystore> load(const std::filesystem::path& path);

 private:
  std::unordered_map<std::uint8_t, crypto::KeyPair> keys_;
  std::unordered_map<std::string, crypto::KeyPair> accounts_;
};

}  // namespace phantomx::wallet
