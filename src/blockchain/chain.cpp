#include "phantomx/blockchain/chain.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/common/serialization.hpp"
#include "phantomx/primitives/hash.hpp"
#include "phantomx/primitives/merkle.hpp"
#include "phantomx/utxo/validation.hpp"

namespace phantomx::blockchain {
namespace {

std::string hex_hash(const Hash256Bytes& h) {
  return to_hex(std::span<const std::uint8_t>(h.data(), h.size()));
}

Result<Hash256Bytes> hash_from_hex(std::string_view hex) {
  std::vector<std::uint8_t> bytes;
  if (!from_hex(hex, bytes) || bytes.size() != 32) return MakeError(ErrorCode::kParseError, "invalid hash hex");
  Hash256Bytes out{};
  std::copy(bytes.begin(), bytes.end(), out.begin());
  return out;
}

std::vector<std::uint8_t> encode_undo(const BlockUndoData& undo) {
  BinaryWriter w;
  w.write_bytes(std::span<const std::uint8_t>(undo.block_hash.data(), undo.block_hash.size()));
  w.write_u32(static_cast<std::uint32_t>(undo.spent.size()));
  for (const auto& e : undo.spent) {
    w.write_bytes(std::span<const std::uint8_t>(e.outpoint.txid.data(), e.outpoint.txid.size()));
    w.write_u32(e.outpoint.vout);
    w.write_u64(e.utxo.amount_atoms);
    w.write_u8(e.utxo.coinbase ? 1 : 0);
    w.write_u32(e.utxo.height);
    w.write_bytes(e.utxo.script_pubkey);
  }
  return w.take();
}

Result<BlockUndoData> decode_undo(std::span<const std::uint8_t> bytes) {
  BinaryReader r(bytes);
  BlockUndoData undo;
  auto bh_len = r.read_u64();
  if (!bh_len.ok() || bh_len.value() != 32) return MakeError(ErrorCode::kSerializationError, "invalid undo encoding");
  auto bh = r.read_bytes(32);
  auto count = r.read_u32();
  if (!bh.ok() || !count.ok()) return MakeError(ErrorCode::kSerializationError, "invalid undo encoding");
  std::copy(bh.value().begin(), bh.value().end(), undo.block_hash.begin());
  undo.spent.reserve(count.value());
  for (std::uint32_t i = 0; i < count.value(); ++i) {
    UndoEntry e;
    auto txid_len = r.read_u64();
    if (!txid_len.ok() || txid_len.value() != 32) return MakeError(ErrorCode::kSerializationError, "invalid undo entry encoding");
    auto txidb = r.read_bytes(32);
    auto vout = r.read_u32();
    auto amount = r.read_u64();
    auto coinbase = r.read_u8();
    auto height = r.read_u32();
    auto script_len = r.read_u64();
    if (!txidb.ok() || !vout.ok() || !amount.ok() || !coinbase.ok() || !height.ok() || !script_len.ok()) {
      return MakeError(ErrorCode::kSerializationError, "invalid undo entry encoding");
    }
    auto script = r.read_bytes(static_cast<std::size_t>(script_len.value()));
    if (!script.ok()) return MakeError(ErrorCode::kSerializationError, "invalid undo entry encoding");
    std::copy(txidb.value().begin(), txidb.value().end(), e.outpoint.txid.begin());
    e.outpoint.vout = vout.value();
    e.utxo.amount_atoms = amount.value();
    e.utxo.coinbase = coinbase.value() != 0;
    e.utxo.height = height.value();
    e.utxo.script_pubkey = script.value();
    undo.spent.push_back(std::move(e));
  }
  return undo;
}

}  // namespace

Result<ChainDatabase> ChainDatabase::open(const std::filesystem::path& path) {
  ChainDatabase c;
  c.path_ = path;
  auto db = db::SqliteDb::open(path);
  if (!db.ok()) return db.error();
  c.db_ = std::move(db.value());
  auto init = c.init_schema();
  if (!init.ok()) return init.error();
  auto rec = c.recover_after_crash();
  if (!rec.ok()) return rec.error();
  auto gen = c.apply_genesis_if_needed();
  if (!gen.ok()) return gen.error();
  return c;
}

Result<void> ChainDatabase::close() {
  std::scoped_lock lk(*mu_); return db_.close(); }

Result<void> ChainDatabase::init_schema() {
  std::scoped_lock lk(*mu_);
  return db_.exec(
      "CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
      "CREATE TABLE IF NOT EXISTS blocks(hash TEXT PRIMARY KEY, height INTEGER NOT NULL, prev_hash TEXT NOT NULL, "
      "timestamp INTEGER NOT NULL, header_blob BLOB NOT NULL, block_blob BLOB NOT NULL, active INTEGER NOT NULL DEFAULT 0);"
      "CREATE INDEX IF NOT EXISTS idx_blocks_height ON blocks(height);"
      "CREATE INDEX IF NOT EXISTS idx_blocks_prev ON blocks(prev_hash);"
      "CREATE TABLE IF NOT EXISTS utxos(txid TEXT NOT NULL, vout INTEGER NOT NULL, amount INTEGER NOT NULL, "
      "coinbase INTEGER NOT NULL, height INTEGER NOT NULL, script_pubkey BLOB NOT NULL, PRIMARY KEY(txid,vout));"
      "CREATE TABLE IF NOT EXISTS tx_index(txid TEXT PRIMARY KEY, block_hash TEXT NOT NULL, tx_index INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS undo(block_hash TEXT PRIMARY KEY, undo_blob BLOB NOT NULL);"
      "CREATE TABLE IF NOT EXISTS snapshots(id TEXT PRIMARY KEY, tip_hash TEXT NOT NULL, tip_height INTEGER NOT NULL, "
      "created_at INTEGER NOT NULL, path TEXT NOT NULL);"
      "CREATE TABLE IF NOT EXISTS verifier_challenges(challenge_id TEXT PRIMARY KEY, verifier_id TEXT NOT NULL, pubkey_hex TEXT NOT NULL, owner TEXT NOT NULL, network TEXT NOT NULL, payload_blob BLOB NOT NULL, issued_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, used_at INTEGER, status TEXT NOT NULL);"
      "CREATE INDEX IF NOT EXISTS idx_vch_expires ON verifier_challenges(expires_at);");
}

Result<void> ChainDatabase::set_metadata(std::string_view key, std::string_view value) {
  std::scoped_lock lk(*mu_);
  auto st = db_.prepare("INSERT INTO metadata(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value;");
  if (!st.ok()) return st.error();
  auto b1 = st.value().bind_text(1, key); if (!b1.ok()) return b1;
  auto b2 = st.value().bind_text(2, value); if (!b2.ok()) return b2;
  return st.value().execute();
}

Result<std::optional<std::string>> ChainDatabase::get_metadata(std::string_view key) const {
  std::scoped_lock lk(*mu_);
  auto st = const_cast<db::SqliteDb&>(db_).prepare("SELECT value FROM metadata WHERE key=?;");
  if (!st.ok()) return st.error();
  auto b = st.value().bind_text(1, key); if (!b.ok()) return b.error();
  auto row = st.value().step();
  if (!row.ok()) return row.error();
  if (!row.value()) return std::optional<std::string>{};
  return std::optional<std::string>{st.value().column_text(0)};
}

Result<void> ChainDatabase::put_verifier_challenge(const VerifierChallengeRecord& r) {
  std::scoped_lock lk(*mu_);
  db::TransactionGuard tx(db_);
  auto bt = tx.begin(); if (!bt.ok()) return bt;
  auto st = db_.prepare("INSERT INTO verifier_challenges(challenge_id,verifier_id,pubkey_hex,owner,network,payload_blob,issued_at,expires_at,used_at,status) VALUES(?,?,?,?,?,?,?,?,NULL,'issued')");
  if (!st.ok()) { tx.rollback(); return st.error(); }
  auto b1=st.value().bind_text(1,r.challenge_id); if(!b1.ok()){tx.rollback();return b1;}
  auto b2=st.value().bind_text(2,r.verifier_id); if(!b2.ok()){tx.rollback();return b2;}
  auto b3=st.value().bind_text(3,r.pubkey_hex); if(!b3.ok()){tx.rollback();return b3;}
  auto b4=st.value().bind_text(4,r.owner); if(!b4.ok()){tx.rollback();return b4;}
  auto b5=st.value().bind_text(5,r.network); if(!b5.ok()){tx.rollback();return b5;}
  auto b6=st.value().bind_blob(6,std::span<const std::uint8_t>(r.payload_blob.data(), r.payload_blob.size())); if(!b6.ok()){tx.rollback();return b6;}
  auto b7=st.value().bind_u64(7,r.issued_at); if(!b7.ok()){tx.rollback();return b7;}
  auto b8=st.value().bind_u64(8,r.expires_at); if(!b8.ok()){tx.rollback();return b8;}
  auto ex = st.value().execute(); if(!ex.ok()){tx.rollback();return ex;}
  auto cm = tx.commit(); if(!cm.ok()) return cm;
  return {};
}

Result<void> ChainDatabase::put_verifier_challenge_with_quota(const VerifierChallengeRecord& r,
                                                               std::uint64_t now_ts,
                                                               std::uint64_t global_cap,
                                                               std::uint64_t per_identity_cap,
                                                               std::uint64_t consumed_retention_sec,
                                                               std::uint64_t cleanup_batch) {
  // Single-connection atomic enforcement. BEGIN IMMEDIATE acquires write lock before quota check/insert.
  // This prevents same-DB-connection races under mu_ and concurrent writers through SQLite write serialization.
  // It is NOT a claim of cross-process fairness beyond SQLite transaction semantics; separate-process evidence required.
  static std::atomic<std::uint64_t> g_quota_op_seq{0};
  static std::atomic<std::uint64_t> g_quota_tx_seq{0};
  static std::atomic<std::uint64_t> g_quota_conn_seq{0};
  const std::uint64_t op_id = ++g_quota_op_seq;
#ifdef _WIN32
  const std::uint64_t pid = static_cast<std::uint64_t>(_getpid());
#else
  const std::uint64_t pid = static_cast<std::uint64_t>(::getpid());
#endif
  const std::uint64_t tid = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));

  struct ObsSnapshot { QuotaTxDiagObserver cb{}; bool enabled{false}; std::uint64_t conn_id{0}; };
  auto snap_obs = [&]()->ObsSnapshot {
    std::scoped_lock lk(*mu_);
    if (quota_conn_id_ == 0) quota_conn_id_ = ++g_quota_conn_seq;
    return ObsSnapshot{quota_obs_, quota_obs_enabled_ && static_cast<bool>(quota_obs_), quota_conn_id_};
  };

  struct OrigErr { bool present{false}; int primary{0}; int extended{0}; int rc{0}; std::string api; std::string msg; };
  std::vector<QuotaTxDiagEvent> pending_events;
  pending_events.reserve(32);
  auto emit = [&](std::uint64_t tx_id, std::uint32_t attempt, const char* stage, const char* api, bool ok,
                  const db::SqliteDiag* diag, std::int64_t elapsed_us, bool tx_active, bool rb_attempted, bool rb_ok,
                  const OrigErr* orig, const db::SqliteDiag* rb_diag, const char* final_result) {
    ObsSnapshot s = snap_obs();
    if (!s.enabled || !s.cb) return;
    QuotaTxDiagEvent ev{};
    ev.operation_id = op_id; ev.transaction_id = tx_id; ev.connection_id = s.conn_id; ev.process_id = pid; ev.thread_id = tid;
    ev.retry_attempt = attempt; ev.stage = stage ? stage : ""; ev.sqlite_api = api ? api : ""; ev.ok = ok;
    if (diag) { ev.sqlite_primary = diag->primary; ev.sqlite_extended = diag->extended; ev.sqlite_rc = diag->rc; ev.sqlite_message = diag->message; }
    ev.elapsed_us = elapsed_us; ev.tx_active = tx_active; ev.rollback_attempted = rb_attempted; ev.rollback_ok = rb_ok;
    if (orig && orig->present) {
      ev.has_original_error = true; ev.original_primary = orig->primary; ev.original_extended = orig->extended; ev.original_rc = orig->rc;
      ev.original_api = orig->api; ev.original_message = orig->msg;
    }
    if (rb_diag) {
      ev.rollback_error_present = !(rb_diag->rc == SQLITE_OK && rb_diag->primary == 0 && rb_diag->extended == 0);
      ev.rollback_primary = rb_diag->primary; ev.rollback_extended = rb_diag->extended; ev.rollback_rc = rb_diag->rc;
      ev.rollback_api = rb_diag->api; ev.rollback_message = rb_diag->message;
    }
    ev.final_result = final_result ? final_result : "";
    pending_events.push_back(std::move(ev));
  };
  auto flush_events = [&]() {
    if (pending_events.empty()) return;
    ObsSnapshot s = snap_obs();
    if (!s.enabled || !s.cb) { pending_events.clear(); return; }
    for (const auto& ev : pending_events) { try { s.cb(ev); } catch (...) {} }
    pending_events.clear();
  };

  auto has_code = [](const db::SqliteDiag& d, int primary)->bool { return d.primary == primary; };
  auto is_busy_diag = [&](const db::SqliteDiag& d)->bool {
    return has_code(d, 5) /*SQLITE_BUSY*/ || has_code(d, 6) /*SQLITE_LOCKED*/ || d.message.find("database is locked") != std::string::npos || d.message.find("database is busy") != std::string::npos;
  };
  auto is_retryable_tx = [&](const db::SqliteDiag& d)->bool { return is_busy_diag(d); };

  emit(0, 0, "CONNECTION_OPEN", "sqlite3_open_v2", true, nullptr, 0, false, false, false, nullptr, nullptr, "");

  for (int attempt = 0; attempt < 20; ++attempt) {
    const std::uint64_t tx_id = ++g_quota_tx_seq;
    const auto t0 = std::chrono::steady_clock::now();
    auto us_since = [&]()->std::int64_t { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t0).count(); };
    std::scoped_lock lk(*mu_);
    db::TransactionGuard tx(db_);
    auto bt = tx.begin();
    if (!bt.ok()) {
      const auto diag = db_.last_diag();
      emit(tx_id, attempt, "BEGIN", "sqlite3_exec", false, &diag, us_since(), false, false, false, nullptr, nullptr, "");
      if (is_retryable_tx(diag) && attempt < 19) {
        flush_events();
        std::this_thread::sleep_for(std::chrono::milliseconds(1 + (attempt * 2)));
        continue;
      }
      emit(tx_id, attempt, "FINAL_RESULT", "sqlite3_exec", false, &diag, us_since(), false, false, false, nullptr, nullptr, "error");
      flush_events();
      return bt;
    }
    emit(tx_id, attempt, "BEGIN", "sqlite3_exec", true, nullptr, us_since(), true, false, false, nullptr, nullptr, "");

    auto handle_stage_error = [&](const char* stage, const char* api, const db::SqliteDiag& diag, const Error& err)->Result<void> {
      OrigErr orig{true, diag.primary, diag.extended, diag.rc, diag.api, diag.message};
      emit(tx_id, attempt, stage, api, false, &diag, us_since(), true, false, false, &orig, nullptr, "");
      if (is_retryable_tx(diag) && attempt < 19) {
        tx.rollback();
        const auto rb_diag = db_.last_diag();
        const bool rb_ok = rb_diag.rc == SQLITE_OK || rb_diag.primary == 0;
        emit(tx_id, attempt, "ROLLBACK", "sqlite3_exec", rb_ok, &rb_diag, us_since(), false, true, rb_ok, &orig, &rb_diag, "retry");
        flush_events();
        std::this_thread::sleep_for(std::chrono::milliseconds(1 + (attempt * 2)));
        return MakeError(ErrorCode::kInternalError, "retry");
      }
      tx.rollback();
      const auto rb_diag = db_.last_diag();
      const bool rb_ok = rb_diag.rc == SQLITE_OK || rb_diag.primary == 0;
      emit(tx_id, attempt, "ROLLBACK", "sqlite3_exec", rb_ok, &rb_diag, us_since(), false, true, rb_ok, &orig, &rb_diag, "error");
      emit(tx_id, attempt, "FINAL_RESULT", api, false, &diag, us_since(), false, true, rb_ok, &orig, &rb_diag, "error");
      flush_events();
      return err;
    };

    // Cleanup inside same tx so quota is evaluated against consistent state.
    {
      const std::uint64_t cutoff = (now_ts > consumed_retention_sec) ? (now_ts - consumed_retention_sec) : 0;
      auto stc = db_.prepare("DELETE FROM verifier_challenges WHERE challenge_id IN (SELECT challenge_id FROM verifier_challenges WHERE (expires_at < ? AND used_at IS NULL) OR (used_at IS NOT NULL AND used_at < ?) LIMIT ?);");
      if(!stc.ok()) { auto hr=handle_stage_error("CLEANUP","sqlite3_prepare_v2", db_.last_diag(), stc.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto c1 = stc.value().bind_u64(1, now_ts); if(!c1.ok()){ auto hr=handle_stage_error("CLEANUP","sqlite3_bind_int64", db_.last_diag(), c1.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto c2 = stc.value().bind_u64(2, cutoff); if(!c2.ok()){ auto hr=handle_stage_error("CLEANUP","sqlite3_bind_int64", db_.last_diag(), c2.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto c3 = stc.value().bind_u64(3, cleanup_batch); if(!c3.ok()){ auto hr=handle_stage_error("CLEANUP","sqlite3_bind_int64", db_.last_diag(), c3.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto ce = stc.value().execute(); if(!ce.ok()){ auto hr=handle_stage_error("CLEANUP","sqlite3_step", db_.last_diag(), ce.error()); if(hr.error().message()=="retry") continue; return hr; }
      emit(tx_id, attempt, "CLEANUP", "sqlite3_step", true, nullptr, us_since(), true, false, false, nullptr, nullptr, "");
    }
    // Global active cap
    {
      auto stg = db_.prepare("SELECT COUNT(*) FROM verifier_challenges WHERE used_at IS NULL AND expires_at >= ?;");
      if(!stg.ok()) { auto hr=handle_stage_error("GLOBAL_COUNT","sqlite3_prepare_v2", db_.last_diag(), stg.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto g1 = stg.value().bind_u64(1, now_ts); if(!g1.ok()){ auto hr=handle_stage_error("GLOBAL_COUNT","sqlite3_bind_int64", db_.last_diag(), g1.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto gr = stg.value().step(); if(!gr.ok()){ auto hr=handle_stage_error("GLOBAL_COUNT","sqlite3_step", db_.last_diag(), gr.error()); if(hr.error().message()=="retry") continue; return hr; }
      const std::uint64_t gcount = gr.value() ? stg.value().column_u64(0) : 0;
      emit(tx_id, attempt, "GLOBAL_COUNT", "sqlite3_step", true, nullptr, us_since(), true, false, false, nullptr, nullptr, "");
      if (gcount >= global_cap) {
        tx.rollback();
        const auto rb_diag = db_.last_diag();
        const bool rb_ok = rb_diag.rc == SQLITE_OK || rb_diag.primary == 0;
        emit(tx_id, attempt, "QUOTA_CHECK", "business", false, nullptr, us_since(), false, true, rb_ok, nullptr, &rb_diag, "challenge_capacity_global");
        emit(tx_id, attempt, "FINAL_RESULT", "business", false, nullptr, us_since(), false, true, rb_ok, nullptr, &rb_diag, "quota_global");
        flush_events();
        return MakeError(ErrorCode::kInternalError, "challenge_capacity_global");
      }
    }
    // Per-identity active cap (OR verifier_id/owner as current policy; documented).
    {
      auto sti = db_.prepare("SELECT COUNT(*) FROM verifier_challenges WHERE used_at IS NULL AND expires_at >= ? AND (verifier_id=? OR owner=?);");
      if(!sti.ok()) { auto hr=handle_stage_error("IDENTITY_COUNT","sqlite3_prepare_v2", db_.last_diag(), sti.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto i1 = sti.value().bind_u64(1, now_ts); if(!i1.ok()){ auto hr=handle_stage_error("IDENTITY_COUNT","sqlite3_bind_int64", db_.last_diag(), i1.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto i2 = sti.value().bind_text(2, r.verifier_id); if(!i2.ok()){ auto hr=handle_stage_error("IDENTITY_COUNT","sqlite3_bind_text", db_.last_diag(), i2.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto i3 = sti.value().bind_text(3, r.owner); if(!i3.ok()){ auto hr=handle_stage_error("IDENTITY_COUNT","sqlite3_bind_text", db_.last_diag(), i3.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto ir = sti.value().step(); if(!ir.ok()){ auto hr=handle_stage_error("IDENTITY_COUNT","sqlite3_step", db_.last_diag(), ir.error()); if(hr.error().message()=="retry") continue; return hr; }
      const std::uint64_t icount = ir.value() ? sti.value().column_u64(0) : 0;
      emit(tx_id, attempt, "IDENTITY_COUNT", "sqlite3_step", true, nullptr, us_since(), true, false, false, nullptr, nullptr, "");
      if (icount >= per_identity_cap) {
        tx.rollback();
        const auto rb_diag = db_.last_diag();
        const bool rb_ok = rb_diag.rc == SQLITE_OK || rb_diag.primary == 0;
        emit(tx_id, attempt, "QUOTA_CHECK", "business", false, nullptr, us_since(), false, true, rb_ok, nullptr, &rb_diag, "challenge_capacity_identity");
        emit(tx_id, attempt, "FINAL_RESULT", "business", false, nullptr, us_since(), false, true, rb_ok, nullptr, &rb_diag, "quota_identity");
        flush_events();
        return MakeError(ErrorCode::kInternalError, "challenge_capacity_identity");
      }
    }
    emit(tx_id, attempt, "QUOTA_CHECK", "business", true, nullptr, us_since(), true, false, false, nullptr, nullptr, "");
    // Insert
    {
      auto st = db_.prepare("INSERT INTO verifier_challenges(challenge_id,verifier_id,pubkey_hex,owner,network,payload_blob,issued_at,expires_at,used_at,status) VALUES(?,?,?,?,?,?,?,?,NULL,'issued')");
      if (!st.ok()) { auto hr=handle_stage_error("INSERT","sqlite3_prepare_v2", db_.last_diag(), st.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto b1=st.value().bind_text(1,r.challenge_id); if(!b1.ok()){ auto hr=handle_stage_error("INSERT","sqlite3_bind_text", db_.last_diag(), b1.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto b2=st.value().bind_text(2,r.verifier_id); if(!b2.ok()){ auto hr=handle_stage_error("INSERT","sqlite3_bind_text", db_.last_diag(), b2.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto b3=st.value().bind_text(3,r.pubkey_hex); if(!b3.ok()){ auto hr=handle_stage_error("INSERT","sqlite3_bind_text", db_.last_diag(), b3.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto b4=st.value().bind_text(4,r.owner); if(!b4.ok()){ auto hr=handle_stage_error("INSERT","sqlite3_bind_text", db_.last_diag(), b4.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto b5=st.value().bind_text(5,r.network); if(!b5.ok()){ auto hr=handle_stage_error("INSERT","sqlite3_bind_text", db_.last_diag(), b5.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto b6=st.value().bind_blob(6,std::span<const std::uint8_t>(r.payload_blob.data(), r.payload_blob.size())); if(!b6.ok()){ auto hr=handle_stage_error("INSERT","sqlite3_bind_blob", db_.last_diag(), b6.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto b7=st.value().bind_u64(7,r.issued_at); if(!b7.ok()){ auto hr=handle_stage_error("INSERT","sqlite3_bind_int64", db_.last_diag(), b7.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto b8=st.value().bind_u64(8,r.expires_at); if(!b8.ok()){ auto hr=handle_stage_error("INSERT","sqlite3_bind_int64", db_.last_diag(), b8.error()); if(hr.error().message()=="retry") continue; return hr; }
      auto ex = st.value().execute(); if(!ex.ok()){ auto hr=handle_stage_error("INSERT","sqlite3_step", db_.last_diag(), ex.error()); if(hr.error().message()=="retry") continue; return hr; }
      emit(tx_id, attempt, "INSERT", "sqlite3_step", true, nullptr, us_since(), true, false, false, nullptr, nullptr, "");
    }
    auto cm = tx.commit();
    if(!cm.ok()) {
      const auto commit_diag = db_.last_diag();
      OrigErr orig{true, commit_diag.primary, commit_diag.extended, commit_diag.rc, commit_diag.api, commit_diag.message};
      emit(tx_id, attempt, "COMMIT", "sqlite3_exec", false, &commit_diag, us_since(), true, false, false, &orig, nullptr, "");
      // Commit ambiguity: do not blind-retry. Verify full record context before claiming success.
      auto chk = db_.prepare("SELECT verifier_id,pubkey_hex,owner,network,issued_at,expires_at,used_at,status FROM verifier_challenges WHERE challenge_id=? LIMIT 1;");
      if (chk.ok()) {
        auto c1 = chk.value().bind_text(1, r.challenge_id);
        auto step = c1.ok() ? chk.value().step() : Result<bool>(c1.error());
        if (step.ok() && step.value()) {
          const bool same = chk.value().column_text(0) == r.verifier_id &&
                            chk.value().column_text(1) == r.pubkey_hex &&
                            chk.value().column_text(2) == r.owner &&
                            chk.value().column_text(3) == r.network &&
                            chk.value().column_u64(4) == r.issued_at &&
                            chk.value().column_u64(5) == r.expires_at &&
                            chk.value().column_is_null(6) &&
                            chk.value().column_text(7) == "issued";
          if (same) {
            emit(tx_id, attempt, "AMBIGUITY_LOOKUP", "sqlite3_step", true, nullptr, us_since(), false, false, false, &orig, nullptr, "committed");
            emit(tx_id, attempt, "FINAL_RESULT", "sqlite3_step", true, nullptr, us_since(), false, false, false, &orig, nullptr, "ok_ambiguous_commit");
            flush_events();
            return {};
          }
          auto ferr = MakeError(ErrorCode::kIoError, "challenge quota commit ambiguous: conflicting existing record");
          db::SqliteDiag conflict{}; conflict.api = "ambiguity_check"; conflict.message = ferr.message();
          emit(tx_id, attempt, "AMBIGUITY_LOOKUP", "ambiguity_check", false, &conflict, us_since(), false, false, false, &orig, nullptr, "conflict");
          emit(tx_id, attempt, "FINAL_RESULT", "ambiguity_check", false, &conflict, us_since(), false, false, false, &orig, nullptr, "error");
          flush_events();
          return ferr;
        }
        if (!step.ok()) {
          const auto lookup_diag = db_.last_diag();
          auto ferr = MakeError(ErrorCode::kIoError, "challenge quota commit ambiguous/busy: " + cm.error().to_string());
          emit(tx_id, attempt, "AMBIGUITY_LOOKUP", "sqlite3_step", false, &lookup_diag, us_since(), false, false, false, &orig, nullptr, "lookup_error");
          emit(tx_id, attempt, "FINAL_RESULT", "sqlite3_step", false, &lookup_diag, us_since(), false, false, false, &orig, nullptr, "error");
          flush_events();
          return ferr;
        }
        emit(tx_id, attempt, "AMBIGUITY_LOOKUP", "sqlite3_step", true, nullptr, us_since(), false, false, false, &orig, nullptr, "not_found");
        emit(tx_id, attempt, "FINAL_RESULT", "sqlite3_step", false, &commit_diag, us_since(), false, false, false, &orig, nullptr, "error");
        flush_events();
        return cm;
      }
      const auto chk_diag = db_.last_diag();
      auto ferr = MakeError(ErrorCode::kIoError, "challenge quota commit ambiguous/busy: " + cm.error().to_string());
      emit(tx_id, attempt, "AMBIGUITY_LOOKUP", "sqlite3_prepare_v2", false, &chk_diag, us_since(), false, false, false, &orig, nullptr, "prepare_error");
      emit(tx_id, attempt, "FINAL_RESULT", "sqlite3_prepare_v2", false, &chk_diag, us_since(), false, false, false, &orig, nullptr, "error");
      flush_events();
      return ferr;
    }
    emit(tx_id, attempt, "COMMIT", "sqlite3_exec", true, nullptr, us_since(), false, false, false, nullptr, nullptr, "");
    emit(tx_id, attempt, "FINAL_RESULT", "sqlite3_exec", true, nullptr, us_since(), false, false, false, nullptr, nullptr, "ok");
    flush_events();
    return {};
  }
  auto ferr = MakeError(ErrorCode::kIoError, "challenge quota transaction busy/locked after retries");
  db::SqliteDiag d{}; d.message = ferr.message(); d.api = "retry_budget";
  emit(0, 20, "FINAL_RESULT", "retry_budget", false, &d, 0, false, false, false, nullptr, nullptr, "error");
  flush_events();
  return ferr;
}

Result<std::optional<ChainDatabase::VerifierChallengeRecord>> ChainDatabase::get_verifier_challenge(const std::string& challenge_id) const {
  std::scoped_lock lk(*mu_);
  auto st = const_cast<db::SqliteDb&>(db_).prepare("SELECT challenge_id,verifier_id,pubkey_hex,owner,network,payload_blob,issued_at,expires_at,used_at,status FROM verifier_challenges WHERE challenge_id=? LIMIT 1;");
  if (!st.ok()) return st.error();
  auto b = st.value().bind_text(1, challenge_id);
  if (!b.ok()) return b.error();
  auto row = st.value().step();
  if (!row.ok()) return row.error();
  if (!row.value()) return std::optional<VerifierChallengeRecord>{};
  VerifierChallengeRecord r{};
  r.challenge_id = st.value().column_text(0);
  r.verifier_id = st.value().column_text(1);
  r.pubkey_hex = st.value().column_text(2);
  r.owner = st.value().column_text(3);
  r.network = st.value().column_text(4);
  r.payload_blob = st.value().column_blob(5);
  r.issued_at = st.value().column_u64(6);
  r.expires_at = st.value().column_u64(7);
  if (!st.value().column_is_null(8)) r.used_at = st.value().column_u64(8);
  r.status = st.value().column_text(9);
  return std::optional<VerifierChallengeRecord>{std::move(r)};
}

Result<bool> ChainDatabase::consume_verifier_challenge(const std::string& challenge_id,
                                                        const std::string& verifier_id,
                                                        const std::string& pubkey_hex,
                                                        const std::string& owner,
                                                        const std::string& network,
                                                        std::uint64_t now_ts) {
  std::scoped_lock lk(*mu_);
  db::TransactionGuard tx(db_);
  auto bt = tx.begin(); if (!bt.ok()) return bt.error();
  auto st = db_.prepare("UPDATE verifier_challenges SET used_at=?, status='used' WHERE challenge_id=? AND verifier_id=? AND pubkey_hex=? AND owner=? AND network=? AND used_at IS NULL AND expires_at >= ?;");
  if (!st.ok()) { tx.rollback(); return st.error(); }
  auto b1=st.value().bind_u64(1, now_ts); if(!b1.ok()){tx.rollback();return b1.error();}
  auto b2=st.value().bind_text(2, challenge_id); if(!b2.ok()){tx.rollback();return b2.error();}
  auto b3=st.value().bind_text(3, verifier_id); if(!b3.ok()){tx.rollback();return b3.error();}
  auto b4=st.value().bind_text(4, pubkey_hex); if(!b4.ok()){tx.rollback();return b4.error();}
  auto b5=st.value().bind_text(5, owner); if(!b5.ok()){tx.rollback();return b5.error();}
  auto b6=st.value().bind_text(6, network); if(!b6.ok()){tx.rollback();return b6.error();}
  auto b7=st.value().bind_u64(7, now_ts); if(!b7.ok()){tx.rollback();return b7.error();}
  auto ex = st.value().execute(); if(!ex.ok()){tx.rollback();return ex.error();}
  const int changed = sqlite3_changes(db_.handle());
  auto cm = tx.commit(); if(!cm.ok()) return cm.error();
  return changed == 1;
}

Result<std::uint64_t> ChainDatabase::count_active_verifier_challenges(std::uint64_t now_ts) const {
  std::scoped_lock lk(*mu_);
  auto st = const_cast<db::SqliteDb&>(db_).prepare("SELECT COUNT(*) FROM verifier_challenges WHERE used_at IS NULL AND expires_at >= ?;");
  if (!st.ok()) return st.error();
  auto b1 = st.value().bind_u64(1, now_ts); if(!b1.ok()) return b1.error();
  auto row = st.value().step(); if(!row.ok()) return row.error();
  if(!row.value()) return 0ULL;
  return st.value().column_u64(0);
}

Result<std::uint64_t> ChainDatabase::count_active_verifier_challenges_for(std::string_view verifier_id, std::string_view owner, std::uint64_t now_ts) const {
  std::scoped_lock lk(*mu_);
  auto st = const_cast<db::SqliteDb&>(db_).prepare("SELECT COUNT(*) FROM verifier_challenges WHERE used_at IS NULL AND expires_at >= ? AND (verifier_id=? OR owner=?);");
  if (!st.ok()) return st.error();
  auto b1 = st.value().bind_u64(1, now_ts); if(!b1.ok()) return b1.error();
  auto b2 = st.value().bind_text(2, verifier_id); if(!b2.ok()) return b2.error();
  auto b3 = st.value().bind_text(3, owner); if(!b3.ok()) return b3.error();
  auto row = st.value().step(); if(!row.ok()) return row.error();
  if(!row.value()) return 0ULL;
  return st.value().column_u64(0);
}

Result<void> ChainDatabase::cleanup_verifier_challenges(std::uint64_t now_ts, std::uint64_t consumed_retention_sec, std::uint64_t max_delete) {
  std::scoped_lock lk(*mu_);
  db::TransactionGuard tx(db_);
  auto bt = tx.begin(); if(!bt.ok()) return bt;
  const std::uint64_t cutoff = (now_ts > consumed_retention_sec) ? (now_ts - consumed_retention_sec) : 0;
  auto st = db_.prepare("DELETE FROM verifier_challenges WHERE challenge_id IN (SELECT challenge_id FROM verifier_challenges WHERE (expires_at < ? AND used_at IS NULL) OR (used_at IS NOT NULL AND used_at < ?) LIMIT ?);");
  if(!st.ok()) { tx.rollback(); return st.error(); }
  auto b1 = st.value().bind_u64(1, now_ts); if(!b1.ok()){tx.rollback(); return b1;}
  auto b2 = st.value().bind_u64(2, cutoff); if(!b2.ok()){tx.rollback(); return b2;}
  auto b3 = st.value().bind_u64(3, max_delete); if(!b3.ok()){tx.rollback(); return b3;}
  auto ex = st.value().execute(); if(!ex.ok()){tx.rollback(); return ex;}
  auto cm = tx.commit(); if(!cm.ok()) return cm;
  return {};
}

Result<std::optional<ChainTip>> ChainDatabase::tip() const {
  std::scoped_lock lk(*mu_);
  auto h = get_metadata("tip_hash");
  auto he = get_metadata("tip_height");
  if (!h.ok()) return h.error();
  if (!he.ok()) return he.error();
  if (!h.value().has_value() || !he.value().has_value()) return std::optional<ChainTip>{};
  auto hash = hash_from_hex(*h.value());
  if (!hash.ok()) return hash.error();
  ChainTip t;
  t.hash = hash.value();
  t.height = static_cast<std::uint32_t>(std::stoul(*he.value()));
  return std::optional<ChainTip>{t};
}

Result<void> ChainDatabase::write_block(const Block& block) {
  std::scoped_lock lk(*mu_);
  const auto hash = block_hash(block.header);
  auto existing = get_block_index(hash);
  if (!existing.ok()) return existing.error();
  if (existing.value().has_value()) return Result<void>{}; // idempotent

  auto st = db_.prepare(
      "INSERT INTO blocks(hash,height,prev_hash,timestamp,header_blob,block_blob,active) VALUES(?,?,?,?,?,?,0);");
  if (!st.ok()) return st.error();
  auto hb = st.value().bind_text(1, hex_hash(hash)); if (!hb.ok()) return hb;
  auto h2 = st.value().bind_u32(2, block.header.height); if (!h2.ok()) return h2;
  auto h3 = st.value().bind_text(3, hex_hash(block.header.prev_hash)); if (!h3.ok()) return h3;
  auto h4 = st.value().bind_u64(4, block.header.timestamp); if (!h4.ok()) return h4;
  auto header_blob = serialize_block_header(block.header);
  auto block_blob = serialize_block(block);
  auto h5 = st.value().bind_blob(5, header_blob); if (!h5.ok()) return h5;
  auto h6 = st.value().bind_blob(6, block_blob); if (!h6.ok()) return h6;
  return st.value().execute();
}

Result<Block> ChainDatabase::read_block(const Hash256Bytes& hash) const {
  std::scoped_lock lk(*mu_);
  auto st = const_cast<db::SqliteDb&>(db_).prepare("SELECT block_blob FROM blocks WHERE hash=?;");
  if (!st.ok()) return st.error();
  auto b = st.value().bind_text(1, hex_hash(hash)); if (!b.ok()) return b.error();
  auto row = st.value().step();
  if (!row.ok()) return row.error();
  if (!row.value()) return MakeError(ErrorCode::kNotFound, "block not found");
  auto blob = st.value().column_blob(0);
  auto parsed = deserialize_block(blob);
  if (!parsed.ok()) return parsed.error();
  return parsed.value();
}

Result<std::optional<BlockIndexEntry>> ChainDatabase::get_block_index(const Hash256Bytes& hash) const {
  std::scoped_lock lk(*mu_);
  auto st = const_cast<db::SqliteDb&>(db_).prepare(
      "SELECT hash,prev_hash,height,timestamp,active,block_blob IS NOT NULL FROM blocks WHERE hash=?;");
  if (!st.ok()) return st.error();
  auto b = st.value().bind_text(1, hex_hash(hash)); if (!b.ok()) return b.error();
  auto row = st.value().step();
  if (!row.ok()) return row.error();
  if (!row.value()) return std::optional<BlockIndexEntry>{};
  BlockIndexEntry e;
  auto hh = hash_from_hex(st.value().column_text(0)); if (!hh.ok()) return hh.error();
  auto ph = hash_from_hex(st.value().column_text(1)); if (!ph.ok()) return ph.error();
  e.hash = hh.value();
  e.prev_hash = ph.value();
  e.height = st.value().column_u32(2);
  e.timestamp = st.value().column_u64(3);
  e.active = st.value().column_int64(4) != 0;
  e.has_block_data = st.value().column_int64(5) != 0;
  return std::optional<BlockIndexEntry>{e};
}

Result<std::optional<Hash256Bytes>> ChainDatabase::get_hash_by_height(std::uint32_t height) const {
  std::scoped_lock lk(*mu_);
  auto st = const_cast<db::SqliteDb&>(db_).prepare("SELECT hash FROM blocks WHERE height=? AND active=1 LIMIT 1;");
  if (!st.ok()) return st.error();
  auto b = st.value().bind_u32(1, height); if (!b.ok()) return b.error();
  auto row = st.value().step();
  if (!row.ok()) return row.error();
  if (!row.value()) return std::optional<Hash256Bytes>{};
  auto h = hash_from_hex(st.value().column_text(0)); if (!h.ok()) return h.error();
  return std::optional<Hash256Bytes>{h.value()};
}
Result<void> ChainDatabase::validate_block(const Block& block) const {
  std::scoped_lock lk(*mu_);
  if (block.transactions.empty()) return MakeError(ErrorCode::kInvalidArgument, "missing_block");
  auto current_tip = tip();
  if (!current_tip.ok()) return current_tip.error();
  if (current_tip.value().has_value()) {
    if (block.header.prev_hash != current_tip.value()->hash) return MakeError(ErrorCode::kInvalidArgument, "bad_prev_hash");
    if (block.header.height != current_tip.value()->height + 1) return MakeError(ErrorCode::kInvalidArgument, "bad_height");
    if (block.header.timestamp <  current_tip.value()->height /* deterministic lower-bound guard */) return MakeError(ErrorCode::kInvalidArgument, "bad_timestamp");
  }
  if (block.transactions[0].kind != kTxKindCoinbase) return MakeError(ErrorCode::kInvalidArgument, "bad_coinbase");
  if (block.transactions[0].inputs.empty() || block.transactions[0].inputs[0].prevout.vout != 0xffffffffu)
    return MakeError(ErrorCode::kInvalidArgument, "bad_coinbase");
  // Consensus rule: generatetoaddress/dev coinbase marker is allowed only up to height 100.
  if (block.header.height > 100) {
    const auto& ss = block.transactions[0].inputs[0].script_sig;
    const std::string prefix = "PhantomX-";
    if (ss.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), ss.begin())) {
      return MakeError(ErrorCode::kInvalidArgument, "generatetoaddress not allowed above height 100");
    }
  }

  std::vector<Hash256Bytes> txids;
  txids.reserve(block.transactions.size());
  for (const auto& tx : block.transactions) {
    auto basic = validate_transaction_basic(tx);
    if (!basic.ok) return basic.error;
    txids.push_back(txid(tx));
  }
  const auto mr = merkle_root(std::move(txids));
  if (mr != block.header.merkle_root) return MakeError(ErrorCode::kInvalidArgument, "bad_merkle");

  // Contextual validation against current UTXO set for active tip's child only is handled by apply path.
  // For explicit validation, we check txs against current utxos.
  auto utxos_r = load_utxo_set();
  if (!utxos_r.ok()) return utxos_r.error();
  const auto& utxos = utxos_r.value();
  std::uint64_t fees = 0;
  for (std::size_t i = 1; i < block.transactions.size(); ++i) {
    auto v = validate_transaction(block.transactions[i], utxos, true, block.header.height);
    if (!v.ok) return v.error;
    if (UINT64_MAX - fees < v.fee_atoms) return MakeError(ErrorCode::kInvalidArgument, "fee overflow");
    fees += v.fee_atoms;
  }
  return Result<void>{};
}

Result<BlockUndoData> ChainDatabase::build_undo(const Block& block) const {
  std::scoped_lock lk(*mu_);
  BlockUndoData undo;
  undo.block_hash = block_hash(block.header);
  for (std::size_t i = 1; i < block.transactions.size(); ++i) {
    const auto& tx = block.transactions[i];
    for (const auto& in : tx.inputs) {
      auto st = const_cast<db::SqliteDb&>(db_).prepare("SELECT amount,coinbase,height,script_pubkey FROM utxos WHERE txid=? AND vout=?;");
      if (!st.ok()) return st.error();
      auto b1 = st.value().bind_text(1, hex_hash(in.prevout.txid)); if (!b1.ok()) return b1.error();
      auto b2 = st.value().bind_u32(2, in.prevout.vout); if (!b2.ok()) return b2.error();
      auto row = st.value().step();
      if (!row.ok()) return row.error();
      if (!row.value()) return MakeError(ErrorCode::kNotFound, "undo: utxo not found");
      UndoEntry e;
      e.outpoint = in.prevout;
      e.utxo.amount_atoms = st.value().column_u64(0);
      e.utxo.coinbase = st.value().column_int64(1) != 0;
      e.utxo.height = st.value().column_u32(2);
      e.utxo.script_pubkey = st.value().column_blob(3);
      undo.spent.push_back(std::move(e));
    }
  }
  return undo;
}

Result<void> ChainDatabase::apply_block(const Block& block) {
  std::scoped_lock lk(*mu_);
  const auto hash = block_hash(block.header);
  auto existing = get_block_index(hash);
  if (!existing.ok()) return existing.error();

  // Validate structural and contextual
  auto val = validate_block(block);
  if (!val.ok()) return val;

  // Ensure parent is current tip or genesis
  auto tip_r = tip();
  if (!tip_r.ok()) return tip_r.error();
  if (tip_r.value().has_value()) {
    if (block.header.prev_hash != tip_r.value()->hash) return MakeError(ErrorCode::kInvalidArgument, "block prev_hash does not match tip");
    if (block.header.height != tip_r.value()->height + 1) return MakeError(ErrorCode::kInvalidArgument, "block height mismatch");
  } else {
    // only genesis allowed
    bool is_genesis = block.header.height == 0 && std::all_of(block.header.prev_hash.begin(), block.header.prev_hash.end(), [](std::uint8_t b){return b==0;});
    if (!is_genesis) return MakeError(ErrorCode::kInvalidArgument, "first applied block must be genesis");
  }

  auto undo_r = build_undo(block);
  if (!undo_r.ok()) return undo_r.error();
  auto undo_blob = encode_undo(undo_r.value());

  db::TransactionGuard tx(db_);
  auto begin = tx.begin(); if (!begin.ok()) return begin;

  if (!existing.value().has_value()) {
    auto w = write_block(block); if (!w.ok()) { tx.rollback(); return w; }
  }

  // Mark active
  auto st_active = db_.prepare("UPDATE blocks SET active=1 WHERE hash=?;");
  if (!st_active.ok()) { tx.rollback(); return st_active.error(); }
  auto ba = st_active.value().bind_text(1, hex_hash(hash)); if (!ba.ok()) { tx.rollback(); return ba; }
  auto ea = st_active.value().execute(); if (!ea.ok()) { tx.rollback(); return ea; }

  // Spend inputs
  for (std::size_t i = 1; i < block.transactions.size(); ++i) {
    for (const auto& in : block.transactions[i].inputs) {
      auto st = db_.prepare("DELETE FROM utxos WHERE txid=? AND vout=?;");
      if (!st.ok()) { tx.rollback(); return st.error(); }
      auto b1 = st.value().bind_text(1, hex_hash(in.prevout.txid)); if (!b1.ok()) { tx.rollback(); return b1; }
      auto b2 = st.value().bind_u32(2, in.prevout.vout); if (!b2.ok()) { tx.rollback(); return b2; }
      auto ex = st.value().execute(); if (!ex.ok()) { tx.rollback(); return ex; }
    }
  }

  // Add outputs
  for (const auto& txobj : block.transactions) {
    const auto tid = txid(txobj);
    for (std::uint32_t vout = 0; vout < txobj.outputs.size(); ++vout) {
      const auto& out = txobj.outputs[vout];
      auto st = db_.prepare("INSERT OR REPLACE INTO utxos(txid,vout,amount,coinbase,height,script_pubkey) VALUES(?,?,?,?,?,?);");
      if (!st.ok()) { tx.rollback(); return st.error(); }
      auto b1 = st.value().bind_text(1, hex_hash(tid)); if (!b1.ok()) { tx.rollback(); return b1; }
      auto b2 = st.value().bind_u32(2, vout); if (!b2.ok()) { tx.rollback(); return b2; }
      auto b3 = st.value().bind_u64(3, out.amount_atoms); if (!b3.ok()) { tx.rollback(); return b3; }
      auto b4 = st.value().bind_int(4, txobj.kind == kTxKindCoinbase ? 1 : 0); if (!b4.ok()) { tx.rollback(); return b4; }
      auto b5 = st.value().bind_u32(5, block.header.height); if (!b5.ok()) { tx.rollback(); return b5; }
      auto b6 = st.value().bind_blob(6, out.script_pubkey); if (!b6.ok()) { tx.rollback(); return b6; }
      auto ex = st.value().execute(); if (!ex.ok()) { tx.rollback(); return ex; }
    }
  }

  // Tx index
  for (std::uint32_t i = 0; i < block.transactions.size(); ++i) {
    const auto tid = txid(block.transactions[i]);
    auto st = db_.prepare("INSERT OR REPLACE INTO tx_index(txid,block_hash,tx_index) VALUES(?,?,?);");
    if (!st.ok()) { tx.rollback(); return st.error(); }
    auto b1 = st.value().bind_text(1, hex_hash(tid)); if (!b1.ok()) { tx.rollback(); return b1; }
    auto b2 = st.value().bind_text(2, hex_hash(hash)); if (!b2.ok()) { tx.rollback(); return b2; }
    auto b3 = st.value().bind_u32(3, i); if (!b3.ok()) { tx.rollback(); return b3; }
    auto ex = st.value().execute(); if (!ex.ok()) { tx.rollback(); return ex; }
  }

  // Undo data
  auto st_undo = db_.prepare("INSERT OR REPLACE INTO undo(block_hash,undo_blob) VALUES(?,?);");
  if (!st_undo.ok()) { tx.rollback(); return st_undo.error(); }
  auto u1 = st_undo.value().bind_text(1, hex_hash(hash)); if (!u1.ok()) { tx.rollback(); return u1; }
  auto u2 = st_undo.value().bind_blob(2, undo_blob); if (!u2.ok()) { tx.rollback(); return u2; }
  auto ue = st_undo.value().execute(); if (!ue.ok()) { tx.rollback(); return ue; }

  auto sm1 = set_metadata("tip_hash", hex_hash(hash)); if (!sm1.ok()) { tx.rollback(); return sm1; }
  auto sm2 = set_metadata("tip_height", std::to_string(block.header.height)); if (!sm2.ok()) { tx.rollback(); return sm2; }

  auto c = tx.commit(); if (!c.ok()) return c;
  return Result<void>{};
}

Result<void> ChainDatabase::disconnect_block(const Hash256Bytes& hash, bool allow_finalized) {
  std::scoped_lock lk(*mu_);
  auto tip_r = tip();
  if (!tip_r.ok()) return tip_r.error();
  if (!tip_r.value().has_value()) return MakeError(ErrorCode::kInvalidArgument, "no tip to disconnect");
  if (tip_r.value()->hash != hash) return MakeError(ErrorCode::kInvalidArgument, "can only disconnect current tip");

  auto fin = get_metadata("finalized_height");
  if (!fin.ok()) return fin.error();
  if (fin.value().has_value() && !allow_finalized) {
    const auto finalized_height = static_cast<std::uint32_t>(std::stoul(*fin.value()));
    if (tip_r.value()->height <= finalized_height) {
      return MakeError(ErrorCode::kInvalidArgument, "disconnect refused: block is finalized");
    }
  }

  auto block_r = read_block(hash);
  if (!block_r.ok()) return block_r.error();
  const auto& block = block_r.value();

  auto undo_st = db_.prepare("SELECT undo_blob FROM undo WHERE block_hash=?;");
  if (!undo_st.ok()) return undo_st.error();
  auto ub = undo_st.value().bind_text(1, hex_hash(hash)); if (!ub.ok()) return ub;
  auto urow = undo_st.value().step(); if (!urow.ok()) return urow.error();
  if (!urow.value()) return MakeError(ErrorCode::kNotFound, "undo data not found");
  auto undo_blob = undo_st.value().column_blob(0);
  auto undo_r = decode_undo(undo_blob);
  if (!undo_r.ok()) return undo_r.error();
  const auto& undo = undo_r.value();

  db::TransactionGuard tx(db_);
  auto begin = tx.begin(); if (!begin.ok()) return begin;

  // Remove outputs created by this block
  for (const auto& txobj : block.transactions) {
    const auto tid = txid(txobj);
    for (std::uint32_t vout = 0; vout < txobj.outputs.size(); ++vout) {
      auto st = db_.prepare("DELETE FROM utxos WHERE txid=? AND vout=?;");
      if (!st.ok()) { tx.rollback(); return st.error(); }
      auto b1 = st.value().bind_text(1, hex_hash(tid)); if (!b1.ok()) { tx.rollback(); return b1; }
      auto b2 = st.value().bind_u32(2, vout); if (!b2.ok()) { tx.rollback(); return b2; }
      auto ex = st.value().execute(); if (!ex.ok()) { tx.rollback(); return ex; }
    }
  }

  // Restore spent inputs
  for (const auto& e : undo.spent) {
    auto st = db_.prepare("INSERT OR REPLACE INTO utxos(txid,vout,amount,coinbase,height,script_pubkey) VALUES(?,?,?,?,?,?);");
    if (!st.ok()) { tx.rollback(); return st.error(); }
    auto b1 = st.value().bind_text(1, hex_hash(e.outpoint.txid)); if (!b1.ok()) { tx.rollback(); return b1; }
    auto b2 = st.value().bind_u32(2, e.outpoint.vout); if (!b2.ok()) { tx.rollback(); return b2; }
    auto b3 = st.value().bind_u64(3, e.utxo.amount_atoms); if (!b3.ok()) { tx.rollback(); return b3; }
    auto b4 = st.value().bind_int(4, e.utxo.coinbase ? 1 : 0); if (!b4.ok()) { tx.rollback(); return b4; }
    auto b5 = st.value().bind_u32(5, e.utxo.height); if (!b5.ok()) { tx.rollback(); return b5; }
    auto b6 = st.value().bind_blob(6, e.utxo.script_pubkey); if (!b6.ok()) { tx.rollback(); return b6; }
    auto ex = st.value().execute(); if (!ex.ok()) { tx.rollback(); return ex; }
  }

  // Remove tx index entries
  auto st_del_tx = db_.prepare("DELETE FROM tx_index WHERE block_hash=?;");
  if (!st_del_tx.ok()) { tx.rollback(); return st_del_tx.error(); }
  auto dt1 = st_del_tx.value().bind_text(1, hex_hash(hash)); if (!dt1.ok()) { tx.rollback(); return dt1; }
  auto dte = st_del_tx.value().execute(); if (!dte.ok()) { tx.rollback(); return dte; }

  // Mark block inactive
  auto st_inactive = db_.prepare("UPDATE blocks SET active=0 WHERE hash=?;");
  if (!st_inactive.ok()) { tx.rollback(); return st_inactive.error(); }
  auto ia = st_inactive.value().bind_text(1, hex_hash(hash)); if (!ia.ok()) { tx.rollback(); return ia; }
  auto ie = st_inactive.value().execute(); if (!ie.ok()) { tx.rollback(); return ie; }

  // Update tip to prev
  if (block.header.height == 0) {
    auto d1 = db_.exec("DELETE FROM metadata WHERE key='tip_hash';"); if (!d1.ok()) { tx.rollback(); return d1; }
    auto d2 = db_.exec("DELETE FROM metadata WHERE key='tip_height';"); if (!d2.ok()) { tx.rollback(); return d2; }
  } else {
    auto sm1 = set_metadata("tip_hash", hex_hash(block.header.prev_hash)); if (!sm1.ok()) { tx.rollback(); return sm1; }
    auto sm2 = set_metadata("tip_height", std::to_string(block.header.height - 1)); if (!sm2.ok()) { tx.rollback(); return sm2; }
  }

  auto c = tx.commit(); if (!c.ok()) return c;
  return Result<void>{};
}

Result<UtxoSet> ChainDatabase::load_utxo_set() const {
  std::scoped_lock lk(*mu_);
  UtxoSet set;
  auto st = const_cast<db::SqliteDb&>(db_).prepare("SELECT txid,vout,amount,coinbase,height,script_pubkey FROM utxos;");
  if (!st.ok()) return st.error();
  while (true) {
    auto row = st.value().step();
    if (!row.ok()) return row.error();
    if (!row.value()) break;
    auto txh = hash_from_hex(st.value().column_text(0)); if (!txh.ok()) return txh.error();
    OutPoint op;
    op.txid = txh.value();
    op.vout = st.value().column_u32(1);
    UtxoEntry e;
    e.amount_atoms = st.value().column_u64(2);
    e.coinbase = st.value().column_int64(3) != 0;
    e.height = st.value().column_u32(4);
    e.script_pubkey = st.value().column_blob(5);
    set.add(op, std::move(e));
  }
  return set;
}

Result<std::optional<UtxoEntry>> ChainDatabase::get_utxo(const OutPoint& outpoint) const {
  std::scoped_lock lk(*mu_);
  auto st = const_cast<db::SqliteDb&>(db_).prepare("SELECT amount,coinbase,height,script_pubkey FROM utxos WHERE txid=? AND vout=?;");
  if (!st.ok()) return st.error();
  auto b1 = st.value().bind_text(1, hex_hash(outpoint.txid)); if (!b1.ok()) return b1.error();
  auto b2 = st.value().bind_u32(2, outpoint.vout); if (!b2.ok()) return b2.error();
  auto row = st.value().step();
  if (!row.ok()) return row.error();
  if (!row.value()) return std::optional<UtxoEntry>{};
  UtxoEntry e;
  e.amount_atoms = st.value().column_u64(0);
  e.coinbase = st.value().column_int64(1) != 0;
  e.height = st.value().column_u32(2);
  e.script_pubkey = st.value().column_blob(3);
  return std::optional<UtxoEntry>{e};
}

Result<void> ChainDatabase::recover_after_crash() {
  std::scoped_lock lk(*mu_);
  // SQLite WAL+FULL synchronous gives atomicity; recovery ensures metadata consistency.
  auto ic = db_.integrity_check();
  if (!ic.ok()) return ic.error();
  // Ensure tip metadata points to an active block if present.
  auto tip_r = tip();
  if (!tip_r.ok()) return tip_r.error();
  if (tip_r.value().has_value()) {
    auto idx = get_block_index(tip_r.value()->hash);
    if (!idx.ok()) return idx.error();
    if (!idx.value().has_value() || !idx.value()->active) {
      return MakeError(ErrorCode::kIoError, "chainstate inconsistent: tip not active");
    }
  }
  return Result<void>{};
}

Result<void> ChainDatabase::verify_database() const {
  std::scoped_lock lk(*mu_);
  auto ic = db_.integrity_check();
  if (!ic.ok()) return ic;
  // Verify active chain continuity and utxo consistency basics.
  auto tip_r = tip();
  if (!tip_r.ok()) return tip_r.error();
  if (!tip_r.value().has_value()) return Result<void>{};
  std::uint32_t h = tip_r.value()->height;
  auto hash = tip_r.value()->hash;
  for (std::uint32_t i = 0; i <= h; ++i) {
    auto idx = get_block_index(hash);
    if (!idx.ok()) return idx.error();
    if (!idx.value().has_value()) return MakeError(ErrorCode::kIoError, "verify: missing block index");
    if (idx.value()->height != h - i) return MakeError(ErrorCode::kIoError, "verify: height mismatch");
    if (!idx.value()->active) return MakeError(ErrorCode::kIoError, "verify: inactive block on active chain");
    auto blk = read_block(hash);
    if (!blk.ok()) return blk.error();
    auto calc = block_hash(blk.value().header);
    if (calc != hash) return MakeError(ErrorCode::kIoError, "verify: block hash mismatch");
    if (blk.value().header.height == 0) break;
    hash = blk.value().header.prev_hash;
  }
  return Result<void>{};
}

Result<void> ChainDatabase::backup(const std::filesystem::path& backup_path) const {
  std::scoped_lock lk(*mu_);
  if (backup_path.empty()) return MakeError(ErrorCode::kInvalidArgument, "backup path empty");
  std::error_code ec;
  std::filesystem::create_directories(backup_path.parent_path(), ec);
  return const_cast<db::SqliteDb&>(db_).backup_to(backup_path);
}

Result<void> ChainDatabase::restore(const std::filesystem::path& backup_path) {
  std::scoped_lock lk(*mu_);
  if (!std::filesystem::exists(backup_path)) return MakeError(ErrorCode::kNotFound, "backup not found");
  auto ic = db::SqliteDb::open(backup_path);
  if (!ic.ok()) return ic.error();
  auto check = ic.value().integrity_check();
  if (!check.ok()) return check.error();
  auto c = close(); if (!c.ok()) return c;
  std::error_code ec;
  std::filesystem::copy_file(backup_path, path_, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) return MakeError(ErrorCode::kIoError, "restore copy failed: " + ec.message());
  auto reopened = db::SqliteDb::open(path_);
  if (!reopened.ok()) return reopened.error();
  db_ = std::move(reopened.value());
  return recover_after_crash();
}

Result<SnapshotMeta> ChainDatabase::create_snapshot(const std::filesystem::path& snapshot_path) const {
  std::scoped_lock lk(*mu_);
  auto b = backup(snapshot_path);
  if (!b.ok()) return b.error();
  auto tip_r = tip();
  if (!tip_r.ok()) return tip_r.error();
  SnapshotMeta m;
  m.id = snapshot_path.filename().string();
  m.path = snapshot_path.string();
  m.created_at = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
  if (tip_r.value().has_value()) {
    m.tip_hash = tip_r.value()->hash;
    m.tip_height = tip_r.value()->height;
  }
  auto st = const_cast<db::SqliteDb&>(db_).prepare("INSERT OR REPLACE INTO snapshots(id,tip_hash,tip_height,created_at,path) VALUES(?,?,?,?,?);");
  if (!st.ok()) return st.error();
  auto b1 = st.value().bind_text(1, m.id); if (!b1.ok()) return b1.error();
  auto b2 = st.value().bind_text(2, hex_hash(m.tip_hash)); if (!b2.ok()) return b2.error();
  auto b3 = st.value().bind_u32(3, m.tip_height); if (!b3.ok()) return b3.error();
  auto b4 = st.value().bind_u64(4, m.created_at); if (!b4.ok()) return b4.error();
  auto b5 = st.value().bind_text(5, m.path); if (!b5.ok()) return b5.error();
  auto ex = st.value().execute(); if (!ex.ok()) return ex.error();
  return m;
}

Result<void> ChainDatabase::verify_snapshot(const std::filesystem::path& snapshot_path) const {
  std::scoped_lock lk(*mu_);
  if (!std::filesystem::exists(snapshot_path)) return MakeError(ErrorCode::kNotFound, "snapshot not found");
  auto snap = db::SqliteDb::open(snapshot_path);
  if (!snap.ok()) return snap.error();
  auto check = snap.value().integrity_check();
  if (!check.ok()) return check.error();
  return Result<void>{};
}

Result<void> ChainDatabase::apply_genesis_if_needed() {
  std::scoped_lock lk(*mu_);
  auto tip_r = tip();
  if (!tip_r.ok()) return tip_r.error();
  if (tip_r.value().has_value()) return Result<void>{};
  // Apply regtest genesis by default for foundation; callers can set metadata/network later.
  auto net_meta = get_metadata("network");
  if (!net_meta.ok()) return net_meta.error();
  Network net = Network::kRegtest;
  if (net_meta.value().has_value()) {
    if (*net_meta.value() == "mainnet") net = Network::kMainnet;
    else if (*net_meta.value() == "testnet") net = Network::kTestnet;
    else net = Network::kRegtest;
  } else {
    auto sm = set_metadata("network", "regtest");
    if (!sm.ok()) return sm;
  }
  auto g = genesis_block(net);
  auto w = write_block(g);
  if (!w.ok()) return w;
  return apply_block(g);
}

}  // namespace phantomx::blockchain
