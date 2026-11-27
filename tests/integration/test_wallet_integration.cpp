#include <array>
#include <filesystem>
#include <vector>

#include "../test_framework.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/primitives/bech32m.hpp"
#include "phantomx/utxo/tx_builder.hpp"
#include "phantomx/utxo/validation.hpp"
#include "phantomx/wallet/keystore.hpp"

PHX_TEST(wallet_integration_creation_recovery_signing) {
  auto dir = std::filesystem::temp_directory_path() / "phantomx-wallet-integration";
  std::filesystem::create_directories(dir);
  auto path = dir / "wallet.keystore";

  phantomx::wallet::Keystore ks;
  auto g = ks.generate(phantomx::crypto::KeyRole::kWalletSigning);
  PHX_EXPECT(g.ok());
  PHX_EXPECT(ks.save(path).ok());

  auto loaded = phantomx::wallet::Keystore::load(path);
  PHX_EXPECT(loaded.ok());
  PHX_EXPECT(loaded.value().has(phantomx::crypto::KeyRole::kWalletSigning));

  auto kp = loaded.value().get(phantomx::crypto::KeyRole::kWalletSigning);
  PHX_EXPECT(kp.has_value());
  std::vector<std::uint8_t> msg = {1,2,3,4};
  auto sig = phantomx::crypto::sign(msg, kp->sec);
  PHX_EXPECT(sig.ok());
  PHX_EXPECT(phantomx::crypto::verify(msg, sig.value(), kp->pub));

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

PHX_TEST(wallet_integration_send_receive_flow) {
  std::array<std::uint8_t, 32> sender_seed{};
  std::array<std::uint8_t, 32> receiver_seed{};
  for (std::size_t i = 0; i < 32; ++i) {
    sender_seed[i] = static_cast<std::uint8_t>(i + 1);
    receiver_seed[i] = static_cast<std::uint8_t>(i + 101);
  }
  auto sender = phantomx::crypto::keypair_from_seed(phantomx::crypto::KeyRole::kWalletSigning, sender_seed);
  auto receiver = phantomx::crypto::keypair_from_seed(phantomx::crypto::KeyRole::kWalletSigning, receiver_seed);
  PHX_EXPECT(sender.ok() && receiver.ok());

  auto recv_addr = phantomx::encode_phantomx_address(
      "rphx", 0,
      std::span<const std::uint8_t>(receiver.value().pub.bytes.data(), 20));
  PHX_EXPECT(recv_addr.ok()); // receive address

  auto sender_spk = phantomx::script_pubkey_for_pubkey(
      std::vector<std::uint8_t>(sender.value().pub.bytes.begin(), sender.value().pub.bytes.end()));
  auto receiver_spk = phantomx::script_pubkey_for_pubkey(
      std::vector<std::uint8_t>(receiver.value().pub.bytes.begin(), receiver.value().pub.bytes.end()));

  phantomx::OutPoint prev{};
  prev.txid.fill(0x42);
  prev.vout = 0;
  phantomx::UtxoSet utxos;
  utxos.add(prev, phantomx::UtxoEntry{500000ULL, sender_spk, false, 1});

  phantomx::TransactionBuilder b;
  b.add_input({prev, *utxos.get(prev), sender.value().sec, sender.value().pub});
  b.add_output({490000ULL, receiver_spk});
  auto tx = b.build_and_sign();
  PHX_EXPECT(tx.ok());

  auto v = phantomx::validate_transaction(tx.value(), utxos, true);
  PHX_EXPECT(v.ok);
  PHX_EXPECT(recv_addr.value().size() > 0);
}

int main() { return phantomx::test::run_all(); }
