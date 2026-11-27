#include <filesystem>
#include <jansson.h>
#include "../test_framework.hpp"
#include "phantomx/blockchain/chain.hpp"
#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/aggregator/aggregator.hpp"
#include "phantomx/pool/pool.hpp"
#include "phantomx/primitives/bech32m.hpp"
#include "phantomx/primitives/hash.hpp"
#include "phantomx/primitives/transaction.hpp"
#include "phantomx/rpc/node_handlers.hpp"
#include "phantomx/rpc/rpc.hpp"
#include "phantomx/utxo/mempool.hpp"
#include "phantomx/utxo/tx_builder.hpp"
#include "phantomx/utxo/validation.hpp"

using namespace phantomx;
namespace {
std::string tmp(const char* n){ auto p=std::filesystem::temp_directory_path()/"phantomx_regtest_e2e"/n; std::error_code ec; std::filesystem::remove_all(p,ec); std::filesystem::create_directories(p); return p.string(); }
rpc::RpcResponse call(rpc::RpcServer& s, const std::string& method, std::unordered_map<std::string,std::string> params={}){
  rpc::RpcRequest r; r.id="1"; r.method=method; r.params=std::move(params); r.auth_token="t"; return s.call(r,0);
}
}
PHX_TEST(regtest_e2e_generate_send_mempool_confirm){
  PHX_REQUIRE(crypto::init().ok());
  auto dir=tmp("db1");
  auto chain = blockchain::ChainDatabase::open(std::filesystem::path(dir)/"chain.db");
  PHX_REQUIRE(chain.ok());
  Mempool mempool;
  pool::PoolLedger pool_ledger;
  aggregator::AggregationEngine aggregator;
  rpc::RpcServer server; rpc::register_default_methods(server);
  server.add_user({"u","t",{}});
  PHX_REQUIRE(rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool_ledger, aggregator).ok());

  auto miner = crypto::generate_keypair(crypto::KeyRole::kWalletSigning);
  auto recv  = crypto::generate_keypair(crypto::KeyRole::kWalletSigning);
  PHX_REQUIRE(miner.ok() && recv.ok());
  auto miner_addr = encode_phantomx_address(address_hrp(Network::kRegtest), 0, miner.value().pub.bytes);
  auto recv_addr  = encode_phantomx_address(address_hrp(Network::kRegtest), 0, recv.value().pub.bytes);
  PHX_REQUIRE(miner_addr.ok() && recv_addr.ok());

  auto g = call(server, "generatetoaddress", {{"address",miner_addr.value()}, {"nblocks","101"}});
PHX_EXPECT(g.ok);
  auto bal = call(server, "getbalance", {{"public_key_hex", crypto::serialize_public_key_hex(miner.value().pub)}});
  PHX_EXPECT(bal.ok);
  PHX_EXPECT(std::stoull(bal.result.at("atoms")) > 0);

  // Build send tx from miner UTXOs
  auto utxos = chain.value().load_utxo_set();
  PHX_REQUIRE(utxos.ok());
  auto tipr = chain.value().tip();
  PHX_REQUIRE(tipr.ok() && tipr.value().has_value());
  const auto spend_h = tipr.value()->height + 1;
  const auto spk = script_pubkey_for_pubkey(std::vector<std::uint8_t>(miner.value().pub.bytes.begin(), miner.value().pub.bytes.end()));
  TransactionBuilder b;
  uint64_t in_sum=0;
  for(const auto& [op,ue] : utxos.value().entries()){
    if(ue.script_pubkey != spk) continue;
    if(ue.coinbase && (spend_h < ue.height || (spend_h - ue.height) < kCoinbaseMaturity)) continue;
    b.add_input(BuilderInput{op,ue,miner.value().sec,miner.value().pub}); in_sum+=ue.amount_atoms; break;
  }
  PHX_REQUIRE(in_sum > 200000000ULL);
  auto dest_spk = script_pubkey_for_address(recv_addr.value(), address_hrp(Network::kRegtest));
  PHX_REQUIRE(dest_spk.ok());
  b.add_output(BuilderOutput{100000000ULL, dest_spk.value()});
  b.add_output(BuilderOutput{in_sum-100000000ULL-kMinFeeAtoms, spk});
  auto tx = b.build_and_sign();
  PHX_REQUIRE(tx.ok());
  auto raw = to_hex(serialize_transaction(tx.value()));
  auto sent = call(server, "sendrawtransaction", {{"hex", raw}});
PHX_EXPECT(sent.ok);
  PHX_EXPECT(mempool.size() == 1);

  auto g2 = call(server, "generatetoaddress", {{"address",miner_addr.value()}, {"nblocks","1"}});
  PHX_EXPECT(g2.ok);
  PHX_EXPECT(mempool.size() == 0);

  auto bal2 = call(server, "getbalance", {{"public_key_hex", crypto::serialize_public_key_hex(recv.value().pub)}});
  PHX_EXPECT(bal2.ok);
  PHX_EXPECT_EQ(std::stoull(bal2.result.at("atoms")), 100000000ULL);
}

int main(){ return phantomx::test::run_all(); }
