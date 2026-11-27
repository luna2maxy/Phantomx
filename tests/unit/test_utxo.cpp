#include <array>
#include <cstdint>
#include <vector>

#include "../test_framework.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/primitives/hash.hpp"
#include "phantomx/primitives/transaction.hpp"
#include "phantomx/utxo/mempool.hpp"
#include "phantomx/utxo/tx_builder.hpp"
#include "phantomx/utxo/validation.hpp"

namespace {
std::array<std::uint8_t, 32> seed_n(std::uint8_t n) {
  std::array<std::uint8_t, 32> s{};
  for (std::size_t i = 0; i < 32; ++i) s[i] = static_cast<std::uint8_t>(n + i);
  return s;
}

phantomx::crypto::KeyPair kp_n(phantomx::crypto::KeyRole role, std::uint8_t n) {
  auto s = seed_n(n);
  auto kp = phantomx::crypto::keypair_from_seed(role, std::span<const std::uint8_t, 32>(s));
  PHX_EXPECT(kp.ok());
  return kp.value();
}

phantomx::OutPoint make_outpoint(std::uint8_t fill, std::uint32_t vout) {
  phantomx::OutPoint p;
  p.txid.fill(fill);
  p.vout = vout;
  return p;
}
}  // namespace

PHX_TEST(utxo_valid_transfer) {
  auto sender = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 1);
  auto receiver = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 2);
  phantomx::UtxoSet utxos;
  auto spk = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(sender.pub.bytes.begin(), sender.pub.bytes.end()));
  phantomx::OutPoint op = make_outpoint(0x11, 0);
  utxos.add(op, phantomx::UtxoEntry{5'000'000'000ULL, spk, false, 1});

  phantomx::TransactionBuilder b;
  b.add_input({op, *utxos.get(op), sender.sec, sender.pub});
  auto recv_spk = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(receiver.pub.bytes.begin(), receiver.pub.bytes.end()));
  b.add_output({4'999'990'000ULL, recv_spk});
  auto txr = b.build_and_sign();
  PHX_EXPECT(txr.ok());
  auto vr = phantomx::validate_transaction(txr.value(), utxos, true);
  PHX_EXPECT(vr.ok);
  PHX_EXPECT_EQ(static_cast<long long>(vr.fee_atoms), 10'000LL);
}

PHX_TEST(utxo_invalid_signature) {
  auto sender = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 1);
  auto receiver = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 2);
  auto attacker = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 3);
  phantomx::UtxoSet utxos;
  auto spk = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(sender.pub.bytes.begin(), sender.pub.bytes.end()));
  phantomx::OutPoint op = make_outpoint(0x22, 0);
  utxos.add(op, phantomx::UtxoEntry{5'000'000'000ULL, spk, false, 1});
  phantomx::TransactionBuilder b;
  b.add_input({op, *utxos.get(op), attacker.sec, attacker.pub}); // wrong key signs
  auto recv_spk = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(receiver.pub.bytes.begin(), receiver.pub.bytes.end()));
  b.add_output({4'999'990'000ULL, recv_spk});
  auto txr = b.build_and_sign();
  PHX_EXPECT(txr.ok()); // builder signs, but with wrong key for utxo
  auto vr = phantomx::validate_transaction(txr.value(), utxos, true);
  PHX_EXPECT(!vr.ok);
}

PHX_TEST(utxo_double_spend_in_tx) {
  auto sender = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 1);
  auto receiver = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 2);
  phantomx::UtxoSet utxos;
  auto spk = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(sender.pub.bytes.begin(), sender.pub.bytes.end()));
  phantomx::OutPoint op = make_outpoint(0x33, 0);
  utxos.add(op, phantomx::UtxoEntry{5'000'000'000ULL, spk, false, 1});
  phantomx::Transaction tx;
  tx.version = phantomx::kTxVersionV1;
  tx.kind = phantomx::kTxKindStandard;
  phantomx::TxIn in1; in1.prevout = op; tx.inputs.push_back(in1);
  phantomx::TxIn in2; in2.prevout = op; tx.inputs.push_back(in2); // duplicate
  phantomx::TxOut out; out.amount_atoms = 1'000'000ULL; out.script_pubkey = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(receiver.pub.bytes.begin(), receiver.pub.bytes.end())); tx.outputs.push_back(out);
  auto vr = phantomx::validate_transaction_basic(tx);
  PHX_EXPECT(!vr.ok);
}

PHX_TEST(utxo_insufficient_input) {
  auto sender = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 1);
  auto receiver = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 2);
  phantomx::UtxoSet utxos;
  auto spk = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(sender.pub.bytes.begin(), sender.pub.bytes.end()));
  phantomx::OutPoint op = make_outpoint(0x44, 0);
  utxos.add(op, phantomx::UtxoEntry{1'000'000ULL, spk, false, 1});
  phantomx::TransactionBuilder b;
  b.add_input({op, *utxos.get(op), sender.sec, sender.pub});
  auto recv_spk = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(receiver.pub.bytes.begin(), receiver.pub.bytes.end()));
  b.add_output({2'000'000ULL, recv_spk});
  auto txr = b.build_and_sign();
  PHX_EXPECT(!txr.ok());
}

PHX_TEST(utxo_invalid_output) {
  auto sender = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 1);
  phantomx::UtxoSet utxos;
  auto spk = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(sender.pub.bytes.begin(), sender.pub.bytes.end()));
  phantomx::OutPoint op = make_outpoint(0x55, 0);
  utxos.add(op, phantomx::UtxoEntry{5'000'000'000ULL, spk, false, 1});
  phantomx::Transaction tx;
  tx.version = phantomx::kTxVersionV1; tx.kind = phantomx::kTxKindStandard;
  phantomx::TxIn in; in.prevout = op; tx.inputs.push_back(in);
  phantomx::TxOut out; out.amount_atoms = 0; out.script_pubkey = spk; tx.outputs.push_back(out); // invalid amount
  auto vr = phantomx::validate_transaction_basic(tx);
  PHX_EXPECT(!vr.ok);
}

PHX_TEST(utxo_overflow) {
  auto sender = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 1);
  auto spk = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(sender.pub.bytes.begin(), sender.pub.bytes.end()));
  phantomx::Transaction tx;
  tx.version = phantomx::kTxVersionV1; tx.kind = phantomx::kTxKindStandard;
  phantomx::TxIn in; in.prevout = make_outpoint(0x66,0); tx.inputs.push_back(in);
  phantomx::TxOut o1; o1.amount_atoms = UINT64_MAX; o1.script_pubkey = spk; tx.outputs.push_back(o1);
  phantomx::TxOut o2; o2.amount_atoms = 1; o2.script_pubkey = spk; tx.outputs.push_back(o2);
  auto vr = phantomx::validate_transaction_basic(tx);
  PHX_EXPECT(!vr.ok);
}

PHX_TEST(utxo_serialization_roundtrip) {
  auto sender = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 1);
  auto receiver = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 2);
  phantomx::UtxoSet utxos;
  auto spk = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(sender.pub.bytes.begin(), sender.pub.bytes.end()));
  phantomx::OutPoint op = make_outpoint(0x77, 0);
  utxos.add(op, phantomx::UtxoEntry{5'000'000'000ULL, spk, false, 1});
  phantomx::TransactionBuilder b;
  b.add_input({op, *utxos.get(op), sender.sec, sender.pub});
  b.add_output({4'999'990'000ULL, phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(receiver.pub.bytes.begin(), receiver.pub.bytes.end()))});
  auto txr = b.build_and_sign();
  PHX_EXPECT(txr.ok());
  auto bytes = phantomx::serialize_transaction(txr.value());
  auto parsed = phantomx::deserialize_transaction(bytes);
  PHX_EXPECT(parsed.ok());
  PHX_EXPECT(phantomx::to_hex(phantomx::txid(parsed.value())) == phantomx::to_hex(phantomx::txid(txr.value())));
  auto vr = phantomx::validate_transaction(parsed.value(), utxos, true);
  PHX_EXPECT(vr.ok);
}

PHX_TEST(utxo_mempool_conflict) {
  auto sender = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 1);
  auto receiver = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 2);
  auto receiver2 = kp_n(phantomx::crypto::KeyRole::kWalletSigning, 3);
  phantomx::UtxoSet utxos;
  auto spk = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(sender.pub.bytes.begin(), sender.pub.bytes.end()));
  phantomx::OutPoint op = make_outpoint(0x88, 0);
  utxos.add(op, phantomx::UtxoEntry{5'000'000'000ULL, spk, false, 1});

  phantomx::TransactionBuilder b1;
  b1.add_input({op, *utxos.get(op), sender.sec, sender.pub});
  b1.add_output({4'999'990'000ULL, phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(receiver.pub.bytes.begin(), receiver.pub.bytes.end()))});
  auto tx1 = b1.build_and_sign(); PHX_EXPECT(tx1.ok());

  phantomx::TransactionBuilder b2;
  b2.add_input({op, *utxos.get(op), sender.sec, sender.pub});
  b2.add_output({4'999'980'000ULL, phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(receiver2.pub.bytes.begin(), receiver2.pub.bytes.end()))});
  auto tx2 = b2.build_and_sign(); PHX_EXPECT(tx2.ok());

  phantomx::Mempool mp;
  auto r1 = mp.add(tx1.value(), utxos, true);
  PHX_EXPECT(r1.ok());
  auto r2 = mp.add(tx2.value(), utxos, true);
  PHX_EXPECT(!r2.ok()); // conflict: same outpoint spent
}
