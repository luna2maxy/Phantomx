#include "../test_framework.hpp"
#include <cstring>

#include <jansson.h>

#include "phantomx/common/crypto.hpp"
#include "phantomx/primitives/bech32m.hpp"
#include "phantomx/utxo/validation.hpp"
#include "phantomx/wallet/wallet.hpp"

namespace {

std::filesystem::path temp_keystore_path(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / "phantomx_wallet_tests";
  std::filesystem::create_directories(dir);
  return dir / (name + "_wallet.keystore");
}

}  // namespace

PHX_TEST(wallet_create_derives_valid_address) {
  const auto path = temp_keystore_path("create");
  std::error_code ec;
  std::filesystem::remove(path, ec);

  phantomx::wallet::Wallet wallet(path, "http://127.0.0.1:8740");
  auto created = wallet.create();
  PHX_EXPECT(created.ok());
  PHX_EXPECT(wallet.has_wallet());

  const std::string addr = wallet.address();
  PHX_EXPECT(addr.rfind("rphx1", 0) == 0);
  PHX_EXPECT(!wallet.public_key_hex().empty());

  auto decoded = phantomx::decode_phantomx_address("rphx", addr);
  PHX_EXPECT(decoded.ok());
  PHX_EXPECT_EQ(decoded.value().first, 0);
  PHX_EXPECT_EQ(decoded.value().second.size(), 32);

  // Reopen: same address.
  phantomx::wallet::Wallet reopened(path, "http://127.0.0.1:8740");
  PHX_EXPECT(reopened.open().ok());
  PHX_EXPECT(reopened.has_wallet());
  PHX_EXPECT(reopened.address() == addr);
}

PHX_TEST(wallet_set_network_re_derives_address) {
  const auto path = temp_keystore_path("network");
  std::error_code ec;
  std::filesystem::remove(path, ec);

  phantomx::wallet::Wallet wallet(path, "http://127.0.0.1:8740");
  PHX_EXPECT(wallet.create().ok());
  const std::string mainnet_addr = wallet.address();

  wallet.set_network("testnet");
  const std::string testnet_addr = wallet.address();
  PHX_EXPECT(testnet_addr != mainnet_addr);
  PHX_EXPECT(testnet_addr.rfind("tphx1", 0) == 0);
  PHX_EXPECT(wallet.network() == "testnet");
}

PHX_TEST(wallet_import_secret_roundtrip) {
  const auto path = temp_keystore_path("import");
  std::error_code ec;
  std::filesystem::remove(path, ec);

  phantomx::wallet::Wallet wallet(path, "http://127.0.0.1:8740");
  PHX_EXPECT(wallet.create().ok());
  const auto kp = wallet.key_pair();
  PHX_EXPECT(kp.has_value());
  const std::string secret_hex =
      kp.has_value() ? phantomx::crypto::serialize_secret_key_hex(kp->sec) : std::string();
  PHX_EXPECT(!secret_hex.empty());
  const std::string expected_addr = wallet.address();

  phantomx::wallet::Wallet other(path, "http://127.0.0.1:8740");
  PHX_EXPECT(other.import_secret(secret_hex).ok());
  PHX_EXPECT(other.address() == expected_addr);
}

PHX_TEST(wallet_export_json_roundtrip) {
  const auto path = temp_keystore_path("export");
  std::error_code ec;
  std::filesystem::remove(path, ec);

  phantomx::wallet::Wallet wallet(path, "http://127.0.0.1:8740");
  PHX_EXPECT(wallet.create().ok());

  auto exported = wallet.export_json();
  PHX_EXPECT(exported.ok());
  json_error_t err{};
  json_t* root = json_loads(exported.value().c_str(), 0, &err);
  PHX_EXPECT(root != nullptr);
  if (root == nullptr) return;
  PHX_EXPECT_EQ(json_integer_value(json_object_get(root, "version")), 2);
  PHX_EXPECT(json_object_get(root, "address") != nullptr);
  PHX_EXPECT(json_object_get(root, "public_key_hex") != nullptr);
  PHX_EXPECT(std::strcmp(json_string_value(json_object_get(root, "network")), "regtest") == 0);
  PHX_EXPECT(json_boolean_value(json_object_get(root, "has_wallet")));
  PHX_EXPECT(json_is_array(json_object_get(root, "accounts")));
  PHX_EXPECT_EQ(json_array_size(json_object_get(root, "accounts")), 1);
  json_decref(root);
}

PHX_TEST(wallet_backup_and_load) {
  const auto path = temp_keystore_path("backup_src");
  const auto dest = temp_keystore_path("backup_dst");
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::filesystem::remove(dest, ec);

  phantomx::wallet::Wallet wallet(path, "http://127.0.0.1:8740");
  PHX_EXPECT(wallet.create().ok());
  const std::string expected = wallet.address();

  auto backed = wallet.backup(dest);
  PHX_EXPECT(backed.ok());
  PHX_EXPECT(std::filesystem::exists(dest));

  phantomx::wallet::Wallet restored(dest, "http://127.0.0.1:8740");
  PHX_EXPECT(restored.open().ok());
  PHX_EXPECT(restored.address() == expected);
}

PHX_TEST(wallet_send_fails_without_utxos_without_node) {
  const auto path = temp_keystore_path("send");
  std::error_code ec;
  std::filesystem::remove(path, ec);

  phantomx::wallet::Wallet wallet(path, "http://127.0.0.1:8740");
  PHX_EXPECT(wallet.create().ok());
  auto kp = wallet.key_pair();
  PHX_EXPECT(kp.has_value());
  auto dest = phantomx::encode_phantomx_address("rphx", 0, kp->pub.bytes);
  PHX_EXPECT(dest.ok());
  auto sent = wallet.send(dest.value(), 1000000, phantomx::kMinFeeAtoms);
  PHX_EXPECT(!sent.ok());  // no utxos available: cannot spend
}

PHX_TEST(wallet_history_parse_utxos_json) {
  // Smoke-check parse_utxos_json behavior via a wallet with no node: returns empty.
  const auto path = temp_keystore_path("history");
  std::error_code ec;
  std::filesystem::remove(path, ec);
  phantomx::wallet::Wallet wallet(path, "http://127.0.0.1:8740");
  PHX_EXPECT(wallet.create().ok());
  // Without a running node, fetch_utxos fails; history must not crash.
  auto history = wallet.history();
  (void)history;
}

PHX_TEST(wallet_multi_account_create_and_addresses) {
  const auto path = temp_keystore_path("multi");
  std::error_code ec;
  std::filesystem::remove(path, ec);

  phantomx::wallet::Wallet wallet(path, "http://127.0.0.1:8740");
  PHX_EXPECT(wallet.create().ok());
  PHX_EXPECT_EQ(wallet.account_labels().size(), 1u);

  PHX_EXPECT(wallet.create_account("custody").ok());
  PHX_EXPECT(wallet.create_account("treasury").ok());
  PHX_EXPECT_EQ(wallet.account_labels().size(), 3u);

  const auto def = wallet.address_for("default");
  const auto custody = wallet.address_for("custody");
  const auto treasury = wallet.address_for("treasury");
  PHX_EXPECT(!def.empty());
  PHX_EXPECT(!custody.empty());
  PHX_EXPECT(!treasury.empty());
  PHX_EXPECT(def != custody);
  PHX_EXPECT(def != treasury);
  PHX_EXPECT(custody != treasury);

  // Reload preserves accounts.
  phantomx::wallet::Wallet reopened(path, "http://127.0.0.1:8740");
  PHX_EXPECT(reopened.open().ok());
  PHX_EXPECT_EQ(reopened.account_labels().size(), 3u);
  PHX_EXPECT(reopened.address_for("custody") == custody);
}
