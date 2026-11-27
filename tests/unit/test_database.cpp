#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "../test_framework.hpp"
#include "phantomx/blockchain/chain.hpp"
#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/primitives/merkle.hpp"

namespace {

std::filesystem::path tmp_db(const std::string& name) {
  auto p = std::filesystem::temp_directory_path() / ("phantomx-" + name + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
  std::error_code ec;
  std::filesystem::remove(p, ec);
  std::filesystem::remove(p.string() + "-wal", ec);
  std::filesystem::remove(p.string() + "-shm", ec);
  return p;
}

phantomx::Block make_coinbase_only_block(std::uint32_t height, const phantomx::Hash256Bytes& prev) {
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
  out.amount_atoms = 0;
  std::string spk = "out";
  out.script_pubkey.assign(spk.begin(), spk.end());
  cb.outputs.push_back(out);
  b.transactions.push_back(cb);

  std::vector<phantomx::Hash256Bytes> txids;
  txids.push_back(phantomx::txid(cb));
  b.header.merkle_root = phantomx::merkle_root(std::move(txids));
  return b;
}

}  // namespace

PHX_TEST(db_open_genesis_and_read) {
  auto path = tmp_db("genesis");
  auto db = phantomx::blockchain::ChainDatabase::open(path);
  PHX_EXPECT(db.ok());
  auto tip = db.value().tip();
  PHX_EXPECT(tip.ok());
  PHX_EXPECT(tip.value().has_value());
  PHX_EXPECT_EQ(static_cast<long long>(tip.value()->height), 0LL);
  auto genesis = phantomx::genesis_block(phantomx::Network::kRegtest);
  auto read = db.value().read_block(phantomx::block_hash(genesis.header));
  PHX_EXPECT(read.ok());
  PHX_EXPECT_EQ(static_cast<long long>(read.value().header.height), 0LL);
  std::error_code ec; std::filesystem::remove(path, ec);
}

PHX_TEST(db_write_read_block) {
  auto path = tmp_db("write");
  auto db = phantomx::blockchain::ChainDatabase::open(path);
  PHX_EXPECT(db.ok());
  auto genesis = phantomx::genesis_block(phantomx::Network::kRegtest);
  auto b1 = make_coinbase_only_block(1, phantomx::block_hash(genesis.header));
  auto w = db.value().write_block(b1);
  PHX_EXPECT(w.ok());
  auto r = db.value().read_block(phantomx::block_hash(b1.header));
  PHX_EXPECT(r.ok());
  PHX_EXPECT_EQ(static_cast<long long>(r.value().header.height), 1LL);
  std::error_code ec; std::filesystem::remove(path, ec);
}

PHX_TEST(db_apply_disconnect_block) {
  auto path = tmp_db("apply");
  auto db = phantomx::blockchain::ChainDatabase::open(path);
  PHX_EXPECT(db.ok());
  auto genesis = phantomx::genesis_block(phantomx::Network::kRegtest);
  auto b1 = make_coinbase_only_block(1, phantomx::block_hash(genesis.header));
  PHX_EXPECT(db.value().apply_block(b1).ok());
  auto tip = db.value().tip();
  PHX_EXPECT(tip.ok() && tip.value().has_value());
  PHX_EXPECT_EQ(static_cast<long long>(tip.value()->height), 1LL);

  auto d = db.value().disconnect_block(phantomx::block_hash(b1.header));
  PHX_EXPECT(d.ok());
  tip = db.value().tip();
  PHX_EXPECT(tip.ok() && tip.value().has_value());
  PHX_EXPECT_EQ(static_cast<long long>(tip.value()->height), 0LL);
  std::error_code ec; std::filesystem::remove(path, ec);
}

PHX_TEST(db_restart_consistency) {
  auto path = tmp_db("restart");
  {
    auto db = phantomx::blockchain::ChainDatabase::open(path);
    PHX_EXPECT(db.ok());
    auto genesis = phantomx::genesis_block(phantomx::Network::kRegtest);
    auto b1 = make_coinbase_only_block(1, phantomx::block_hash(genesis.header));
    PHX_EXPECT(db.value().apply_block(b1).ok());
    PHX_EXPECT(db.value().close().ok());
  }
  {
    auto db = phantomx::blockchain::ChainDatabase::open(path);
    PHX_EXPECT(db.ok());
    auto tip = db.value().tip();
    PHX_EXPECT(tip.ok() && tip.value().has_value());
    PHX_EXPECT_EQ(static_cast<long long>(tip.value()->height), 1LL);
    auto verify = db.value().verify_database();
    PHX_EXPECT(verify.ok());
    PHX_EXPECT(db.value().close().ok());
  }
  std::error_code ec; std::filesystem::remove(path, ec);
}

PHX_TEST(db_backup_restore) {
  auto path = tmp_db("backup");
  auto backup = tmp_db("backup-copy");
  {
    auto db = phantomx::blockchain::ChainDatabase::open(path);
    PHX_EXPECT(db.ok());
    auto genesis = phantomx::genesis_block(phantomx::Network::kRegtest);
    auto b1 = make_coinbase_only_block(1, phantomx::block_hash(genesis.header));
    PHX_EXPECT(db.value().apply_block(b1).ok());
    PHX_EXPECT(db.value().backup(backup).ok());
    PHX_EXPECT(db.value().close().ok());
  }
  {
    auto db = phantomx::blockchain::ChainDatabase::open(path);
    PHX_EXPECT(db.ok());
    PHX_EXPECT(db.value().restore(backup).ok());
    auto tip = db.value().tip();
    PHX_EXPECT(tip.ok() && tip.value().has_value());
    PHX_EXPECT_EQ(static_cast<long long>(tip.value()->height), 1LL);
    PHX_EXPECT(db.value().close().ok());
  }
  std::error_code ec; std::filesystem::remove(path, ec); std::filesystem::remove(backup, ec);
}

PHX_TEST(db_snapshot_verify) {
  auto path = tmp_db("snap");
  auto snap = tmp_db("snap-file");
  auto db = phantomx::blockchain::ChainDatabase::open(path);
  PHX_EXPECT(db.ok());
  auto meta = db.value().create_snapshot(snap);
  PHX_EXPECT(meta.ok());
  PHX_EXPECT(db.value().verify_snapshot(snap).ok());
  std::error_code ec; std::filesystem::remove(path, ec); std::filesystem::remove(snap, ec);
}

PHX_TEST(db_corrupted_database_detected) {
  auto path = tmp_db("corrupt");
  {
    auto db = phantomx::blockchain::ChainDatabase::open(path);
    PHX_EXPECT(db.ok());
    PHX_EXPECT(db.value().close().ok());
  }
  // Corrupt file
  {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    PHX_EXPECT(f.is_open());
    f.seekp(100);
    char junk[32];
    for (int i = 0; i < 32; ++i) junk[i] = static_cast<char>(i);
    f.write(junk, 32);
    f.close();
  }
  auto db = phantomx::blockchain::ChainDatabase::open(path);
  // Either open/recover fails or integrity check fails; both acceptable as detection.
  if (db.ok()) {
    auto v = db.value().verify_database();
    PHX_EXPECT(!v.ok());
    (void)db.value().close();
  } else {
    PHX_EXPECT(!db.ok());
  }
  std::error_code ec; std::filesystem::remove(path, ec);
}
