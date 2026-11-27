#include "phantomx/wallet/wallet.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <jansson.h>

#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/primitives/bech32m.hpp"
#include "phantomx/utxo/validation.hpp"
#include "phantomx/utxo/tx_builder.hpp"
#include "phantomx/wallet/node_client.hpp"

namespace phantomx::wallet {
using phantomx::rpc::RpcRequest;

namespace {

constexpr const char* kDefaultAccount = "default";

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

std::string resolve_rpc_token(const std::string& configured) {
  if (!configured.empty()) return configured;
  if (const char* env = safe_getenv("PHANTOMX_RPC_TOKEN"); env != nullptr && *env != '\0') return std::string(env);
  return {};
}

Result<phantomx::Network> network_from_string(const std::string& name) {
  if (name == "mainnet") return phantomx::Network::kMainnet;
  if (name == "testnet") return phantomx::Network::kTestnet;
  if (name == "regtest") return phantomx::Network::kRegtest;
  return MakeError(ErrorCode::kInvalidArgument, "unknown network: " + name);
}

Result<std::vector<TransactionInfo>> parse_utxos_json(const std::string& text,
                                                       const std::string& account_label) {
  json_error_t jerr{};
  json_t* arr = json_loads(text.c_str(), 0, &jerr);
  if (arr == nullptr || !json_is_array(arr)) {
    json_decref(arr);
    return MakeError(ErrorCode::kParseError,
                     "invalid utxos json at line " + std::to_string(jerr.line));
  }
  std::vector<TransactionInfo> out;
  for (std::size_t i = 0; i < json_array_size(arr); ++i) {
    json_t* obj = json_array_get(arr, i);
    const json_t* txid = json_object_get(obj, "txid");
    const json_t* vout = json_object_get(obj, "vout");
    const json_t* amount = json_object_get(obj, "amount");
    const json_t* script = json_object_get(obj, "script_pubkey_hex");
    const json_t* height = json_object_get(obj, "height");
    const json_t* immature = json_object_get(obj, "immature");
    if (txid == nullptr || !json_is_string(txid) || script == nullptr || !json_is_string(script)) {
      continue;
    }
    // Skip immature coinbase outputs for spend/history from UTXO RPC.
    if (json_is_true(immature)) continue;
    TransactionInfo info;
    info.account = account_label;
    std::vector<std::uint8_t> txid_bytes;
    if (json_is_number(vout)) info.vout = static_cast<std::uint32_t>(json_integer_value(vout));
    if (json_is_number(amount)) {
      info.amount_atoms = static_cast<std::uint64_t>(json_number_value(amount));
    } else if (json_is_string(amount)) {
      info.amount_atoms = std::stoull(json_string_value(amount));
    }
    if (json_is_number(height)) info.height = static_cast<std::uint32_t>(json_integer_value(height));
    if (from_hex(json_string_value(txid), txid_bytes) && txid_bytes.size() == 32) {
      info.txid = json_string_value(txid);
    }
    if (json_is_string(script)) info.script_pubkey_hex = json_string_value(script);
    out.push_back(std::move(info));
  }
  json_decref(arr);
  return out;
}

}  // namespace

Wallet::Wallet(std::filesystem::path keystore_path, std::string node_rpc)
    : keystore_path_(std::move(keystore_path)), node_rpc_(std::move(node_rpc)) {}

Result<void> Wallet::open() {
  auto loaded = Keystore::load(keystore_path_);
  if (!loaded.ok()) return loaded.error();
  keystore_ = std::move(loaded.value());
  has_wallet_ = !keystore_.account_labels().empty() || keystore_.has(crypto::KeyRole::kWalletSigning);
  return Result<void>{};
}

Result<void> Wallet::create() {
  keystore_ = Keystore{};
  auto generated = keystore_.generate(crypto::KeyRole::kWalletSigning);  // creates "default"
  if (!generated.ok()) return generated.error();
  has_wallet_ = true;
  return save();
}

Result<void> Wallet::create_account(const std::string& label) {
  auto created = keystore_.generate_account(label);
  if (!created.ok()) return created.error();
  has_wallet_ = true;
  return save();
}

Result<void> Wallet::import_secret(std::string_view secret_hex) {
  auto imported = keystore_.import_secret_hex(crypto::KeyRole::kWalletSigning, secret_hex);
  if (!imported.ok()) return imported.error();
  has_wallet_ = true;
  return save();
}

Result<void> Wallet::import_account(const std::string& label, std::string_view secret_hex) {
  auto imported = keystore_.import_account(label, secret_hex);
  if (!imported.ok()) return imported.error();
  has_wallet_ = true;
  return save();
}

Result<void> Wallet::save() {
  has_wallet_ = has_wallet_ || !keystore_.account_labels().empty() ||
                keystore_.has(crypto::KeyRole::kWalletSigning);
  std::error_code ec;
  if (keystore_path_.has_parent_path()) {
    std::filesystem::create_directories(keystore_path_.parent_path(), ec);
    if (ec) {
      return MakeError(ErrorCode::kIoError, "unable to create keystore directory: " + ec.message());
    }
  }
  return keystore_.save(keystore_path_);
}

Result<void> Wallet::backup(const std::filesystem::path& dest) const {
  std::error_code ec;
  if (dest.has_parent_path() && !dest.parent_path().empty()) {
    std::filesystem::create_directories(dest.parent_path(), ec);
  }
  if (!std::filesystem::copy_file(keystore_path_, dest, std::filesystem::copy_options::overwrite_existing,
                                  ec)) {
    return MakeError(ErrorCode::kIoError, "backup failed: " + ec.message());
  }
  return Result<void>{};
}

std::string Wallet::default_account_label() const {
  auto labels = keystore_.account_labels();
  if (labels.empty()) return kDefaultAccount;
  if (std::find(labels.begin(), labels.end(), kDefaultAccount) != labels.end()) return kDefaultAccount;
  return labels.front();
}

std::optional<crypto::KeyPair> Wallet::key_pair() const {
  if (!has_wallet_) return std::nullopt;
  return keystore_.get_account(default_account_label());
}

std::optional<crypto::KeyPair> Wallet::key_pair_for(const std::string& label) const {
  return keystore_.get_account(label);
}

std::string Wallet::address_for_pubkey(const crypto::PublicKey& pub) const {
  Network network = Network::kRegtest;
  if (auto net = network_from_string(network_); net.ok()) network = net.value();
  auto encoded = encode_phantomx_address(address_hrp(network), 0, pub.bytes);
  if (!encoded.ok()) return {};
  return encoded.value();
}

std::string Wallet::address() const {
  const auto kp = key_pair();
  if (!kp.has_value()) return {};
  return address_for_pubkey(kp->pub);
}

std::string Wallet::address_for(const std::string& label) const {
  auto kp = keystore_.get_account(label);
  if (!kp.has_value()) return {};
  return address_for_pubkey(kp->pub);
}

std::string Wallet::public_key_hex() const {
  const auto kp = key_pair();
  if (!kp.has_value()) return {};
  return crypto::serialize_public_key_hex(kp->pub);
}

std::string Wallet::public_key_hex_for(const std::string& label) const {
  auto kp = keystore_.get_account(label);
  if (!kp.has_value()) return {};
  return crypto::serialize_public_key_hex(kp->pub);
}

std::vector<std::string> Wallet::account_labels() const { return keystore_.account_labels(); }

std::vector<AccountInfo> Wallet::accounts() const {
  std::vector<AccountInfo> out;
  for (const auto& acc : keystore_.accounts()) {
    AccountInfo info;
    info.label = acc.label;
    info.public_key_hex = crypto::serialize_public_key_hex(acc.keypair.pub);
    info.address = address_for_pubkey(acc.keypair.pub);
    out.push_back(std::move(info));
  }
  return out;
}

Result<phantomx::rpc::RpcResponse> Wallet::call_rpc(
    const std::string& method, std::unordered_map<std::string, std::string> params) const {
  NodeClient client(node_rpc_);
  RpcRequest req;
  req.method = method;
  req.auth_token = resolve_rpc_token(rpc_token_);
  if (req.auth_token.empty()) return MakeError(ErrorCode::kInvalidArgument, "RPC token not configured (set rpctoken/rpcpassword or PHANTOMX_RPC_TOKEN)");
  req.params = std::move(params);
  auto resp = client.call(req);
  if (!resp.ok()) return resp.error();
  if (!resp.value().ok) {
    return MakeError(ErrorCode::kInternalError,
                     resp.value().error_message.empty() ? "node rpc error" : resp.value().error_message);
  }
  return resp;
}

Result<std::vector<TransactionInfo>> Wallet::fetch_utxos_for_pubkey(
    const std::string& method, const std::string& pubkey_hex, const std::string& account_label) const {
  auto resp = call_rpc(method, {{"public_key_hex", pubkey_hex}});
  if (!resp.ok()) return resp.error();
  if (!resp.value().result.count("utxos_json")) return std::vector<TransactionInfo>{};
  return parse_utxos_json(resp.value().result["utxos_json"], account_label);
}

Result<std::vector<TransactionInfo>> Wallet::fetch_utxos_all(const std::string& method) const {
  std::vector<TransactionInfo> all;
  for (const auto& acc : keystore_.accounts()) {
    const auto pubhex = crypto::serialize_public_key_hex(acc.keypair.pub);
    auto part = fetch_utxos_for_pubkey(method, pubhex, acc.label);
    if (!part.ok()) return part.error();
    all.insert(all.end(), part.value().begin(), part.value().end());
  }
  return all;
}

Result<std::uint64_t> Wallet::balance_atoms() const {
  auto info = balance_info();
  if (!info.ok()) return info.error();
  return info.value().total_atoms();
}

Result<BalanceInfo> Wallet::balance_info() const {
  BalanceInfo out;
  for (const auto& acc : keystore_.accounts()) {
    const auto pubhex = crypto::serialize_public_key_hex(acc.keypair.pub);
    auto resp = call_rpc("getbalance", {{"public_key_hex", pubhex}});
    if (!resp.ok()) return resp.error();
    const auto& r = resp.value().result;
    auto get_u64 = [&r](const char* k) -> std::uint64_t {
      auto it = r.find(k);
      if (it == r.end()) return 0;
      try { return static_cast<std::uint64_t>(std::stoull(it->second)); } catch (...) { return 0; }
    };
    if (r.count("spendable_atoms") || r.count("immature_atoms")) {
      out.spendable_atoms += get_u64("spendable_atoms");
      out.immature_atoms += get_u64("immature_atoms");
    } else {
      out.spendable_atoms += get_u64("atoms");
    }
  }
  return out;
}

Result<std::uint32_t> Wallet::chain_height() const {
  auto resp = call_rpc("getblockcount", {});
  if (!resp.ok()) return resp.error();
  auto it = resp.value().result.find("block_count");
  if (it == resp.value().result.end()) return std::uint32_t{0};
  return static_cast<std::uint32_t>(std::stoul(it->second));
}

Result<std::uint32_t> Wallet::connection_count() const {
  auto resp = call_rpc("getconnectioncount", {});
  if (!resp.ok()) return resp.error();
  auto it = resp.value().result.find("count");
  if (it == resp.value().result.end()) return std::uint32_t{0};
  return static_cast<std::uint32_t>(std::stoul(it->second));
}

Result<std::vector<TransactionInfo>> Wallet::history() const { return fetch_utxos_all("listtransactions"); }

Result<std::vector<TransactionInfo>> Wallet::utxos() const { return fetch_utxos_all("getutxos"); }

Result<std::string> Wallet::send(const std::string& destination, std::uint64_t amount_atoms,
                                 std::uint64_t fee_atoms) const {
  if (keystore_.account_labels().empty()) return MakeError(ErrorCode::kNotFound, "wallet is not open");

  auto network = Network::kRegtest;
  if (auto net = network_from_string(network_); net.ok()) network = net.value();
  auto dest_script = script_pubkey_for_address(destination, address_hrp(network));
  if (!dest_script.ok()) return dest_script.error();

  // Gather UTXOs from all accounts; keep track of which key owns each.
  struct OwnedUtxo {
    TransactionInfo info;
    crypto::KeyPair kp;
  };
  std::vector<OwnedUtxo> owned;
  for (const auto& acc : keystore_.accounts()) {
    const auto pubhex = crypto::serialize_public_key_hex(acc.keypair.pub);
    auto part = fetch_utxos_for_pubkey("getutxos", pubhex, acc.label);
    if (!part.ok()) return part.error();
    for (const auto& u : part.value()) owned.push_back(OwnedUtxo{u, acc.keypair});
  }

  // Greedy: largest first to minimize input count.
  std::sort(owned.begin(), owned.end(),
            [](const OwnedUtxo& a, const OwnedUtxo& b) { return a.info.amount_atoms > b.info.amount_atoms; });

  std::uint64_t total_in = 0;
  TransactionBuilder builder;
  for (const auto& o : owned) {
    if (total_in >= amount_atoms + fee_atoms) break;
    std::vector<std::uint8_t> txid_bytes;
    if (!from_hex(o.info.txid, txid_bytes) || txid_bytes.size() != 32) {
      return MakeError(ErrorCode::kParseError, "node returned an invalid utxo txid");
    }
    Hash256Bytes id{};
    std::copy(txid_bytes.begin(), txid_bytes.end(), id.begin());
    UtxoEntry entry;
    entry.amount_atoms = o.info.amount_atoms;
    if (!from_hex(o.info.script_pubkey_hex, entry.script_pubkey)) {
      return MakeError(ErrorCode::kParseError, "node returned an invalid script_pubkey_hex");
    }
    builder.add_input(BuilderInput{OutPoint{id, o.info.vout}, entry, o.kp.sec, o.kp.pub});
    total_in += o.info.amount_atoms;
  }

  if (total_in < amount_atoms + fee_atoms) {
    return MakeError(ErrorCode::kNotFound,
                     "insufficient funds: balance=" + std::to_string(total_in) +
                         " need=" + std::to_string(amount_atoms + fee_atoms));
  }

  builder.add_output(BuilderOutput{amount_atoms, dest_script.value()});
  const std::uint64_t change = total_in - amount_atoms - fee_atoms;
  if (change > 0) {
    auto change_kp = key_pair();
    if (!change_kp.has_value()) return MakeError(ErrorCode::kNotFound, "no default account for change");
    builder.add_output(BuilderOutput{
        change, script_pubkey_for_pubkey({change_kp->pub.bytes.begin(), change_kp->pub.bytes.end()})});
  }

  auto tx = builder.build_and_sign();
  if (!tx.ok()) return tx.error();

  NodeClient client(node_rpc_);
  RpcRequest req;
  req.method = "sendrawtransaction";
  req.auth_token = resolve_rpc_token(rpc_token_);
  if (req.auth_token.empty()) return MakeError(ErrorCode::kInvalidArgument, "RPC token not configured (set rpctoken/rpcpassword or PHANTOMX_RPC_TOKEN)");
  req.params["hex"] = to_hex(serialize_transaction(tx.value()));
  auto resp = client.call(req);
  if (!resp.ok()) return resp.error();
  if (!resp.value().ok) {
    return MakeError(ErrorCode::kInternalError, resp.value().error_message);
  }
  return resp.value().result.at("txid");
}

Result<std::string> Wallet::export_json() const {
  json_t* root = json_object();
  if (root == nullptr) return MakeError(ErrorCode::kInternalError, "json_object failed");
  json_object_set_new(root, "version", json_integer(2));
  json_object_set_new(root, "address", json_string(address().c_str()));
  json_object_set_new(root, "public_key_hex", json_string(public_key_hex().c_str()));
  json_object_set_new(root, "network", json_string(network().c_str()));
  json_object_set_new(root, "has_wallet", json_boolean(has_wallet_));
  json_object_set_new(root, "default_account", json_string(default_account_label().c_str()));

  json_t* arr = json_array();
  for (const auto& acc : accounts()) {
    json_t* obj = json_object();
    json_object_set_new(obj, "label", json_string(acc.label.c_str()));
    json_object_set_new(obj, "address", json_string(acc.address.c_str()));
    json_object_set_new(obj, "public_key_hex", json_string(acc.public_key_hex.c_str()));
    json_array_append_new(arr, obj);
  }
  json_object_set_new(root, "accounts", arr);

  char* text = json_dumps(root, JSON_COMPACT | JSON_SORT_KEYS);
  std::string out = text != nullptr ? text : "";
  if (text != nullptr) free(text);
  json_decref(root);
  if (out.empty()) return MakeError(ErrorCode::kInternalError, "json_dumps failed");
  return out;
}

}  // namespace phantomx::wallet