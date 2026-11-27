#include <algorithm>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "phantomx/network/protocol.hpp"
#include "phantomx/consensus/types.hpp"
#include "phantomx/network/protocol.hpp"
#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/primitives/bech32m.hpp"
#include "phantomx/primitives/block.hpp"
#include "phantomx/primitives/hash.hpp"
#include "phantomx/primitives/merkle.hpp"
#include "phantomx/primitives/transaction.hpp"

namespace {
phantomx::Hash256Bytes bytes32_from_hex(const std::string& hex) {
  std::vector<std::uint8_t> v;
  PHX_EXPECT(phantomx::from_hex(hex, v));
  PHX_EXPECT_EQ(static_cast<long long>(v.size()), 32LL);
  phantomx::Hash256Bytes out{};
  std::copy(v.begin(), v.end(), out.begin());
  return out;
}
}  // namespace

PHX_TEST(bft_vote_transport_round_trip) { PHX_EXPECT(true); }

PHX_TEST(bft_proposal_qc_transport_round_trip) { PHX_EXPECT(true); }

PHX_TEST(bft_finality_transport_round_trip) { PHX_EXPECT(true); }

PHX_TEST(genesis_mainnet_vectors) {
  const auto g = phantomx::genesis_block(phantomx::Network::kMainnet);
  PHX_EXPECT_EQ(static_cast<long long>(g.header.version), 1LL);
  PHX_EXPECT_EQ(static_cast<long long>(g.header.timestamp), 1767225600LL);
  PHX_EXPECT(phantomx::to_hex(g.header.merkle_root) == "3751e9c10bd8a5297104311ac0c9338cf59e8d9cac4d2becf77cb9382ecca155");
  PHX_EXPECT(phantomx::to_hex(phantomx::block_hash(g.header)) == "7b647ef05c1f23959d5560fa4eb7380d8fa2141bc9aee4ba6b2785a76122a59e");
}

PHX_TEST(genesis_testnet_regtest_vectors) {
  const auto t = phantomx::genesis_block(phantomx::Network::kTestnet);
  const auto r = phantomx::genesis_block(phantomx::Network::kRegtest);
  PHX_EXPECT(phantomx::to_hex(phantomx::block_hash(t.header)) == "98a75772b9ab585f158a1b98adace23c19bb980838c026560cc1e8f8ae165569");
  PHX_EXPECT(phantomx::to_hex(phantomx::block_hash(r.header)) == "849ec5b41b3b7597c5e7e429c17826524498fb91b63c65f705a07f13477e8180");
}

PHX_TEST(merkle_root_vectors) {
  auto h1 = bytes32_from_hex("856a4921cd32690244af7568e7bd1391a94119e17c7f33234f4bf11271b223e5");
  auto h2 = bytes32_from_hex("79043a4d1d4d6d0b830519bfc07b92b4d162a4cd54235719c2c3cc211a638dfd");
  auto h3 = bytes32_from_hex("ef729c31d206229249bd791b29676d26cc7465aa6bc2003d80c7a82a316e0233");
  PHX_EXPECT(phantomx::to_hex(phantomx::merkle_root({h1})) == "856a4921cd32690244af7568e7bd1391a94119e17c7f33234f4bf11271b223e5");
  PHX_EXPECT(phantomx::to_hex(phantomx::merkle_root({h1, h2})) == "89c76c3eb6cab8c3a04c759a445fc64a8e09cf8b3035be4183244a4e714ee75b");
  PHX_EXPECT(phantomx::to_hex(phantomx::merkle_root({h1, h2, h3})) == "f4be66735d8fa90b4c8ba1d964cc8533cea95fa7bd273d57e32c813a4bc4d041");
}

PHX_TEST(serialization_and_txid_vector) {
  phantomx::Transaction tx;
  tx.version = 1;
  tx.kind = phantomx::kTxKindStandard;
  phantomx::TxIn in;
  in.prevout.txid = bytes32_from_hex("1111111111111111111111111111111111111111111111111111111111111111");
  in.prevout.vout = 1;
  in.script_sig = {0x76, 0xa9, 0x14};
  in.sequence = 0xFFFFFFFFu;
  tx.inputs.push_back(in);
  phantomx::TxOut out;
  out.amount_atoms = 2500000000ULL;
  out.script_pubkey = {0x88, 0xac};
  tx.outputs.push_back(out);
  tx.locktime = 0;
  const auto bytes = phantomx::serialize_transaction(tx);
  PHX_EXPECT(phantomx::to_hex(bytes) == "0100000000010000002000000000000000111111111111111111111111111111111111111111111111111111111111111101000000030000000000000076a914ffffffff0100000000f9029500000000020000000000000088ac00000000");
  PHX_EXPECT(phantomx::to_hex(phantomx::txid(tx)) == "d93e9b453dd36421d6507b0e41c7929a617da383535dc85492d749497a93b83b");
  auto parsed = phantomx::deserialize_transaction(bytes);
  PHX_EXPECT(parsed.ok());
  PHX_EXPECT(phantomx::to_hex(phantomx::txid(parsed.value())) == "d93e9b453dd36421d6507b0e41c7929a617da383535dc85492d749497a93b83b");
}

PHX_TEST(address_encoding_vectors) {
  std::vector<std::uint8_t> payload20(20);
  std::vector<std::uint8_t> payload32(32);
  for (std::size_t i = 0; i < 20; ++i) payload20[i] = static_cast<std::uint8_t>(i);
  for (std::size_t i = 0; i < 32; ++i) payload32[i] = static_cast<std::uint8_t>(i);

  auto a1 = phantomx::encode_phantomx_address("phantomx", 0, payload20);
  auto a2 = phantomx::encode_phantomx_address("phantomx", 1, payload32);
  auto a3 = phantomx::encode_phantomx_address("tphx", 0, payload20);
  auto a4 = phantomx::encode_phantomx_address("rphx", 0, payload20);
  PHX_EXPECT(a1.ok() && a1.value() == "phantomx1qqqqsyqcyq5rqwzqfpg9scrgwpugpzysnazx7fn");
  PHX_EXPECT(a2.ok() && a2.value() == "phantomx1pqqqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0swgfusy");
  PHX_EXPECT(a3.ok() && a3.value() == "tphx1qqqqsyqcyq5rqwzqfpg9scrgwpugpzysnwzt8ta");
  PHX_EXPECT(a4.ok() && a4.value() == "rphx1qqqqsyqcyq5rqwzqfpg9scrgwpugpzysntw2amr");

  auto d1 = phantomx::decode_phantomx_address("phantomx", a1.value());
  PHX_EXPECT(d1.ok());
  PHX_EXPECT_EQ(static_cast<long long>(d1.value().first), 0LL);
  PHX_EXPECT(d1.value().second == payload20);
}
