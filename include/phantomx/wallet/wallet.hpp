#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/rpc/rpc.hpp"
#include "phantomx/wallet/keystore.hpp"

namespace phantomx::wallet {

// One row of wallet history/balance state, derived from UTXO entries.
struct TransactionInfo {
  std::string txid;                 // hex of the transaction that created the output
  std::uint32_t vout = 0;
  std::uint64_t amount_atoms = 0;
  std::uint32_t height = 0;
  bool spent = false;
  std::string script_pubkey_hex;    // hex of the script_pubkey (may be empty)
  std::string account;              // account label that owns this UTXO (may be empty for legacy)
};

struct BalanceInfo {
  std::uint64_t spendable_atoms = 0;
  std::uint64_t immature_atoms = 0;
  std::uint64_t total_atoms() const { return spendable_atoms + immature_atoms; }
};

struct AccountInfo {
  std::string label;
  std::string address;
  std::string public_key_hex;
};

// PhantomX wallet abstraction shared by CLI, Qt and any other consumer.
// The wallet itself is local and canonical: it derives addresses and signs
// transactions. Node-facing operations (balance, history, send) go over the
// node's JSON-RPC endpoint via the shared `RpcServer` transport.
class Wallet {
 public:
  Wallet() = default;
  explicit Wallet(std::filesystem::path keystore_path, std::string node_rpc);

  [[nodiscard]] Result<void> open();           // load keystore
  [[nodiscard]] Result<void> create();         // generate + save default account
  [[nodiscard]] Result<void> create_account(const std::string& label);  // add a new account to the keystore
  [[nodiscard]] Result<void> import_secret(std::string_view secret_hex);
  [[nodiscard]] Result<void> import_account(const std::string& label, std::string_view secret_hex);
  [[nodiscard]] Result<void> save();

  [[nodiscard]] bool has_wallet() const { return has_wallet_; }
  [[nodiscard]] const std::filesystem::path& keystore_path() const { return keystore_path_; }
  [[nodiscard]] std::string default_account_label() const;

  // Primary/default account accessors (backward compatible).
  [[nodiscard]] std::optional<crypto::KeyPair> key_pair() const;
  [[nodiscard]] std::string address() const;
  [[nodiscard]] std::string public_key_hex() const;

  // Multi-account accessors.
  [[nodiscard]] std::vector<std::string> account_labels() const;
  [[nodiscard]] std::vector<AccountInfo> accounts() const;
  [[nodiscard]] std::optional<crypto::KeyPair> key_pair_for(const std::string& label) const;
  [[nodiscard]] std::string address_for(const std::string& label) const;
  [[nodiscard]] std::string public_key_hex_for(const std::string& label) const;

  [[nodiscard]] std::string network() const { return network_; }
  void set_network(std::string network) { network_ = std::move(network); }
  [[nodiscard]] const std::string& rpc_token() const { return rpc_token_; }
  void set_rpc_token(std::string token) { rpc_token_ = std::move(token); }

  // Node RPC backed operations (require a running phantomxd).
  // Balance/history/utxos aggregate across all accounts in the keystore.
  [[nodiscard]] Result<std::uint64_t> balance_atoms() const;
  [[nodiscard]] Result<BalanceInfo> balance_info() const;
  [[nodiscard]] Result<std::uint32_t> chain_height() const;
  [[nodiscard]] Result<std::uint32_t> connection_count() const;
  [[nodiscard]] Result<std::vector<TransactionInfo>> history() const;
  // Send gathers UTXOs from all accounts and signs inputs with the owning account key.
  // Change is returned to the default account.
  [[nodiscard]] Result<std::string> send(const std::string& destination, std::uint64_t amount_atoms,
                                         std::uint64_t fee_atoms) const;
  [[nodiscard]] Result<std::vector<TransactionInfo>> utxos() const;
  [[nodiscard]] Result<phantomx::rpc::RpcResponse> call_rpc(
      const std::string& method, std::unordered_map<std::string, std::string> params) const;

  // Creates a JSON document describing the wallet for platforms without a
  // C++ wallet stack (Android/JS/web exports re-import the same secret).
  [[nodiscard]] Result<std::string> export_json() const;

  [[nodiscard]] Result<void> backup(const std::filesystem::path& dest) const;

 private:
  [[nodiscard]] Result<std::vector<TransactionInfo>> fetch_utxos_for_pubkey(
      const std::string& method, const std::string& pubkey_hex, const std::string& account_label) const;
  [[nodiscard]] Result<std::vector<TransactionInfo>> fetch_utxos_all(const std::string& method) const;
  [[nodiscard]] std::string address_for_pubkey(const crypto::PublicKey& pub) const;

  Keystore keystore_;
  std::filesystem::path keystore_path_;
  std::string node_rpc_;
  std::string rpc_token_;
  std::string network_ = "regtest";
  bool has_wallet_ = false;
};

}  // namespace phantomx::wallet
