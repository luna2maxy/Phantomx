#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <span>
#include <thread>
#include <vector>

#include "../test_framework.hpp"
#include "phantomx/blockchain/chain.hpp"
#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/network/node.hpp"
#include "phantomx/network/socket_io.hpp"
#include "phantomx/primitives/block.hpp"
#include "phantomx/primitives/merkle.hpp"
#include "phantomx/utxo/tx_builder.hpp"
#include "phantomx/utxo/validation.hpp"

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

std::filesystem::path tmp_db(const std::string& name) {
  auto p = std::filesystem::temp_directory_path() / ("phantomx-net-" + name + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
  std::error_code ec;
  std::filesystem::remove(p, ec);
  std::filesystem::remove(p.string() + "-wal", ec);
  std::filesystem::remove(p.string() + "-shm", ec);
  return p;
}

phantomx::Block make_coinbase_block(std::uint32_t height, const phantomx::Hash256Bytes& prev,
                                    const std::vector<std::uint8_t>& script_pubkey, std::uint64_t amount) {
  phantomx::Block b;
  b.header.version = phantomx::kBlockVersionV1;
  b.header.prev_hash = prev;
  b.header.timestamp = 1767225600ULL + height;
  b.header.height = height;
  b.header.nonce = height;
  b.header.bits = 0x1d00ffffu;
  phantomx::Transaction cb;
  cb.version = phantomx::kTxVersionV1;
  cb.kind = phantomx::kTxKindCoinbase;
  phantomx::TxIn in;
  in.prevout.txid = {};
  in.prevout.vout = 0xFFFFFFFFu;
  std::string msg = "cb-" + std::to_string(height);
  in.script_sig.assign(msg.begin(), msg.end());
  cb.inputs.push_back(in);
  phantomx::TxOut out;
  out.amount_atoms = amount;
  out.script_pubkey = script_pubkey;
  cb.outputs.push_back(out);
  b.transactions.push_back(cb);
  std::vector<phantomx::Hash256Bytes> txids{phantomx::txid(cb)};
  b.header.merkle_root = phantomx::merkle_root(std::move(txids));
  return b;
}

bool wait_until(std::function<bool()> pred, int ms = 5000) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return pred();
}

}  // namespace

PHX_TEST(p2p_discover_sync_and_tx) {
  auto db1p = tmp_db("n1");
  auto db2p = tmp_db("n2");
  auto c1 = phantomx::blockchain::ChainDatabase::open(db1p);
  auto c2 = phantomx::blockchain::ChainDatabase::open(db2p);
  PHX_EXPECT(c1.ok() && c2.ok());

  phantomx::Mempool mp1;
  phantomx::Mempool mp2;

  auto genesis = phantomx::genesis_block(phantomx::Network::kRegtest);
  auto gh = phantomx::block_hash(genesis.header);

  phantomx::network::NetworkConfig cfg1;
  cfg1.network_magic = 0x52505801u;
  cfg1.genesis_hash = gh;
  cfg1.listen_host = "127.0.0.1";
  cfg1.listen_port = 0;
  cfg1.reconnect_interval_ms = 200;

  phantomx::network::NetworkConfig cfg2 = cfg1;

  phantomx::network::P2PNode n1(cfg1, &c1.value(), &mp1);
  phantomx::network::P2PNode n2(cfg2, &c2.value(), &mp2);
  PHX_EXPECT(n1.start().ok());
  PHX_EXPECT(n2.start().ok());

  // Node2 discovers/connects to node1
  PHX_EXPECT(n2.connect("127.0.0.1", n1.listen_port()).ok());
  PHX_EXPECT(wait_until([&] { return n1.ready_peer_count() >= 1 && n2.ready_peer_count() >= 1; }));

  // Create a spendable block on node1 and sync to node2
  std::array<std::uint8_t, 32> sender_seed{};
  for (std::size_t i = 0; i < 32; ++i) sender_seed[i] = static_cast<std::uint8_t>(i + 1);
  auto sender = phantomx::crypto::keypair_from_seed(phantomx::crypto::KeyRole::kWalletSigning,
      std::span<const std::uint8_t, 32>(sender_seed));
  PHX_EXPECT(sender.ok());
  auto spk = phantomx::script_pubkey_for_pubkey(std::vector<std::uint8_t>(sender.value().pub.bytes.begin(), sender.value().pub.bytes.end()));
  auto b1 = make_coinbase_block(1, gh, spk, 5000000000ULL);
  PHX_EXPECT(c1.value().apply_block(b1).ok());
  PHX_EXPECT(n1.broadcast_block(b1).ok());
  (void)n1.request_sync();
  (void)n2.request_sync();

  const bool synced = wait_until([&] {
    PHX_EXPECT(n1.broadcast_block(b1).ok());
    PHX_EXPECT(n1.request_sync().ok());
    PHX_EXPECT(n2.request_sync().ok());
    auto t = c2.value().tip();
    return t.ok() && t.value().has_value() && t.value()->height >= 1;
  }, 15000);
  PHX_EXPECT(synced);

  // Mature coinbase (kCoinbaseMaturity=100): mine 100 more blocks on node1.
  for (std::uint32_t h = 2; h <= 101; ++h) {
    auto tip = c1.value().tip();
    PHX_EXPECT(tip.ok() && tip.value().has_value());
    if (!tip.ok() || !tip.value().has_value()) break;
    auto bn = make_coinbase_block(h, tip.value()->hash, spk, 5000000000ULL);
    PHX_EXPECT(c1.value().apply_block(bn).ok());
    PHX_EXPECT(n1.broadcast_block(bn).ok());
  }
  PHX_EXPECT(wait_until([&] {
    PHX_EXPECT(n1.request_sync().ok());
    PHX_EXPECT(n2.request_sync().ok());
    auto t = c2.value().tip();
    return t.ok() && t.value().has_value() && t.value()->height >= 101;
  }, 15000));

  // Build and propagate tx from node2 to node1
  std::array<std::uint8_t, 32> receiver_seed{};
  for (std::size_t i = 0; i < 32; ++i) receiver_seed[i] = static_cast<std::uint8_t>(i + 9);
  auto receiver = phantomx::crypto::keypair_from_seed(phantomx::crypto::KeyRole::kWalletSigning,
      std::span<const std::uint8_t, 32>(receiver_seed));
  PHX_EXPECT(receiver.ok());

  bool tx_relayed = false;
  if (synced && receiver.ok()) {
    auto utxos = c2.value().load_utxo_set();
    PHX_EXPECT(utxos.ok());
    if (utxos.ok()) {
      phantomx::OutPoint op;
      op.txid = phantomx::txid(b1.transactions[0]);
      op.vout = 0;
      auto utxo = utxos.value().get(op);
      PHX_EXPECT(utxo.has_value());
      if (utxo.has_value()) {
        phantomx::TransactionBuilder tb;
        tb.add_input({op, *utxo, sender.value().sec, sender.value().pub});
        auto recv_spk = phantomx::script_pubkey_for_pubkey(
            std::vector<std::uint8_t>(receiver.value().pub.bytes.begin(), receiver.value().pub.bytes.end()));
        tb.add_output({4999990000ULL, recv_spk});
        auto txr = tb.build_and_sign();
        PHX_EXPECT(txr.ok());
        if (txr.ok()) {
          auto add = mp2.add(txr.value(), utxos.value(), true, 1000);
          PHX_EXPECT(add.ok());
          if (add.ok()) {
            PHX_EXPECT(n2.broadcast_tx(txr.value()).ok());
            tx_relayed = wait_until([&] { return mp1.size() >= 1; }, 15000);
            PHX_EXPECT(tx_relayed);
          }
        }
      }
    }
  } else {
    PHX_EXPECT(false); // sync prerequisite failed
  }

  n2.stop();
  n1.stop();
  std::error_code ec;
  std::filesystem::remove(db1p, ec); std::filesystem::remove(db1p.string()+"-wal", ec); std::filesystem::remove(db1p.string()+"-shm", ec);
  std::filesystem::remove(db2p, ec); std::filesystem::remove(db2p.string()+"-wal", ec); std::filesystem::remove(db2p.string()+"-shm", ec);
}

#ifndef _WIN32
PHX_TEST(p2p_send_to_closed_socket_does_not_kill_process) {
  int fds[2] = {-1, -1};
  PHX_EXPECT(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  PHX_EXPECT(fds[0] >= 0 && fds[1] >= 0);
  ::close(fds[1]);
  std::array<std::uint8_t, 128> payload{};
  bool ok = true;
  for (int i = 0; i < 8 && ok; ++i) {
    ok = phantomx::network::send_all_bytes(fds[0], payload.data(), payload.size());
  }
  // A peer closing its end must surface as a send error, never as SIGPIPE.
  PHX_EXPECT(!ok);
  ::close(fds[0]);
}
#endif

PHX_TEST(p2p_network_identity_match_connects) {
  auto db1p = tmp_db("idmatch1");
  auto db2p = tmp_db("idmatch2");
  auto c1 = phantomx::blockchain::ChainDatabase::open(db1p);
  auto c2 = phantomx::blockchain::ChainDatabase::open(db2p);
  PHX_EXPECT(c1.ok() && c2.ok());
  phantomx::Mempool mp1, mp2;
  auto genesis = phantomx::genesis_block(phantomx::Network::kTestnet);
  auto gh = phantomx::block_hash(genesis.header);

  phantomx::network::NetworkConfig cfg1;
  cfg1.network_magic = phantomx::network_magic(phantomx::Network::kTestnet);
  cfg1.genesis_hash = gh;
  cfg1.listen_host = "127.0.0.1";
  cfg1.listen_port = 0;
  cfg1.reconnect_interval_ms = 200;

  phantomx::network::NetworkConfig cfg2 = cfg1;

  phantomx::network::P2PNode n1(cfg1, &c1.value(), &mp1);
  phantomx::network::P2PNode n2(cfg2, &c2.value(), &mp2);
  PHX_EXPECT(n1.start().ok());
  PHX_EXPECT(n2.start().ok());
  PHX_EXPECT(n2.connect("127.0.0.1", n1.listen_port()).ok());
  PHX_EXPECT(wait_until([&] { return n1.ready_peer_count() >= 1 && n2.ready_peer_count() >= 1; }, 5000));
  n1.stop();
  n2.stop();
}

PHX_TEST(p2p_network_identity_mismatch_rejected) {
  auto db1p = tmp_db("idmiss1");
  auto db2p = tmp_db("idmiss2");
  auto c1 = phantomx::blockchain::ChainDatabase::open(db1p);
  auto c2 = phantomx::blockchain::ChainDatabase::open(db2p);
  PHX_EXPECT(c1.ok() && c2.ok());
  phantomx::Mempool mp1, mp2;

  auto genesis = phantomx::genesis_block(phantomx::Network::kTestnet);
  auto gh = phantomx::block_hash(genesis.header);

  phantomx::network::NetworkConfig cfg1;
  cfg1.network_magic = phantomx::network_magic(phantomx::Network::kTestnet);
  cfg1.genesis_hash = gh;
  cfg1.listen_host = "127.0.0.1";
  cfg1.listen_port = 0;
  cfg1.reconnect_interval_ms = 200;

  phantomx::network::NetworkConfig cfg2 = cfg1;
  cfg2.network_magic = 0x12345678u;
  phantomx::Hash256Bytes bad_gh{};
  bad_gh[0] = 0xAB;
  cfg2.genesis_hash = bad_gh;

  phantomx::network::P2PNode n1(cfg1, &c1.value(), &mp1);
  phantomx::network::P2PNode n2(cfg2, &c2.value(), &mp2);
  PHX_EXPECT(n1.start().ok());
  PHX_EXPECT(n2.start().ok());
  (void)n2.connect("127.0.0.1", n1.listen_port());
  std::this_thread::sleep_for(std::chrono::seconds(2));
  PHX_EXPECT(n1.ready_peer_count() == 0);
  PHX_EXPECT(n2.ready_peer_count() == 0);
  n1.stop();
  n2.stop();
}

PHX_TEST(p2p_network_identity_zero_genesis_rejected) {
  auto db1p = tmp_db("idzero1");
  auto db2p = tmp_db("idzero2");
  auto c1 = phantomx::blockchain::ChainDatabase::open(db1p);
  auto c2 = phantomx::blockchain::ChainDatabase::open(db2p);
  PHX_EXPECT(c1.ok() && c2.ok());
  phantomx::Mempool mp1, mp2;

  auto genesis = phantomx::genesis_block(phantomx::Network::kTestnet);
  auto gh = phantomx::block_hash(genesis.header);

  phantomx::network::NetworkConfig cfg1;
  cfg1.network_magic = phantomx::network_magic(phantomx::Network::kTestnet);
  cfg1.genesis_hash = gh;
  cfg1.listen_host = "127.0.0.1";
  cfg1.listen_port = 0;
  cfg1.reconnect_interval_ms = 200;

  phantomx::network::NetworkConfig cfg2 = cfg1;
  cfg2.genesis_hash = phantomx::Hash256Bytes{};

  phantomx::network::P2PNode n1(cfg1, &c1.value(), &mp1);
  phantomx::network::P2PNode n2(cfg2, &c2.value(), &mp2);
  PHX_EXPECT(n1.start().ok());
  PHX_EXPECT(n2.start().ok());
  (void)n2.connect("127.0.0.1", n1.listen_port());
  std::this_thread::sleep_for(std::chrono::seconds(2));
  PHX_EXPECT(n1.ready_peer_count() == 0);
  PHX_EXPECT(n2.ready_peer_count() == 0);
  n1.stop();
  n2.stop();
}

int main() { return phantomx::test::run_all(); }
