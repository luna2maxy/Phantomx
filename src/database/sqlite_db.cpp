#include "phantomx/database/sqlite_db.hpp"

#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace phantomx::db {
namespace {
struct ConnObsState {
  std::mutex mu;
  SqliteDb::ConnectionObserver cb{};
  bool enabled{false};
};
ConnObsState& conn_obs_state() {
  static ConnObsState s;
  return s;
}
std::atomic<std::uint64_t> g_conn_seq{0};
std::atomic<std::uint64_t> g_open_attempt_seq{0};
std::atomic<std::uint64_t> g_conn_event_seq{0};

Error make_db_error(ErrorCode code, std::string_view prefix, const SqliteDiag& d) {
  std::string msg(prefix);
  if (!d.message.empty()) { msg += ": "; msg += d.message; }
  msg += " [primary=" + std::to_string(d.primary) + " extended=" + std::to_string(d.extended) + " rc=" + std::to_string(d.rc) + " api=" + d.api + "]";
  return MakeError(code, std::move(msg));
}
SqliteDiag capture_diag(sqlite3* db, std::string_view api, int rc) {
  SqliteDiag d{};
  d.rc = rc;
  d.api = std::string(api);
  if (db != nullptr) {
    d.primary = sqlite3_errcode(db);
    d.extended = sqlite3_extended_errcode(db);
    const char* m = sqlite3_errmsg(db);
    d.message = m ? m : "";
  }
  return d;
}
void emit_conn_event_global(std::uint64_t conn_id, std::uint64_t open_attempt_id, std::uint64_t generation,
                            const std::string& role, const char* stage, const char* op_name, std::uint32_t op_seq,
                            const char* api, bool ok, const SqliteDiag* diag, std::int64_t elapsed_us) {
  ConnObsState& st = conn_obs_state();
  SqliteDb::ConnectionObserver cb;
  bool enabled = false;
  {
    std::scoped_lock lk(st.mu);
    cb = st.cb;
    enabled = st.enabled && static_cast<bool>(st.cb);
  }
  if (!enabled || !cb) return;
  SqliteConnectionEvent ev{};
  ev.monotonic_seq = ++g_conn_event_seq;
  ev.connection_id = conn_id;
  ev.open_attempt_id = open_attempt_id;
  ev.lifecycle_generation = generation;
#ifdef _WIN32
  ev.process_id = static_cast<std::uint64_t>(_getpid());
#else
  ev.process_id = static_cast<std::uint64_t>(::getpid());
#endif
  ev.thread_id = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
  ev.database_role = role;
  ev.stage = stage ? stage : "";
  ev.operation_name = op_name ? op_name : "";
  ev.operation_seq = op_seq;
  ev.sqlite_api = api ? api : "";
  ev.ok = ok;
  if (diag) {
    ev.sqlite_rc = diag->rc; ev.sqlite_primary = diag->primary; ev.sqlite_extended = diag->extended; ev.sqlite_message = diag->message;
  }
  ev.elapsed_us = elapsed_us;
  try { cb(ev); } catch (...) {}
}
}  // namespace

void SqliteDb::set_connection_observer(ConnectionObserver obs, bool enabled) {
  ConnObsState& st = conn_obs_state();
  std::scoped_lock lk(st.mu);
  st.cb = std::move(obs);
  st.enabled = enabled;
}
void SqliteDb::clear_connection_observer() {
  ConnObsState& st = conn_obs_state();
  std::scoped_lock lk(st.mu);
  st.cb = nullptr;
  st.enabled = false;
}
void SqliteDb::emit_conn_event(const char* stage, const char* api, bool ok, const SqliteDiag* diag, std::int64_t elapsed_us) const {
  emit_conn_event_global(connection_id_, open_attempt_id_, lifecycle_generation_, role_, stage, "", 0, api, ok, diag, elapsed_us);
}

Statement::Statement(Statement&& other) noexcept : db_(other.db_), stmt_(other.stmt_), mu_(other.mu_), last_diag_(other.last_diag_) {
  other.db_ = nullptr;
  other.stmt_ = nullptr;
  other.mu_ = nullptr;
  other.last_diag_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
  if (this == &other) return *this;
  if (stmt_ != nullptr) sqlite3_finalize(stmt_);
  db_ = other.db_;
  stmt_ = other.stmt_;
  mu_ = other.mu_;
  last_diag_ = other.last_diag_;
  other.db_ = nullptr;
  other.stmt_ = nullptr;
  other.mu_ = nullptr;
  other.last_diag_ = nullptr;
  return *this;
}

Error Statement::capture_error(ErrorCode code, std::string_view prefix, std::string_view api, int rc) {
  // Capture sqlite3_errcode/extended_errmsg immediately under the same connection lock.
  const SqliteDiag d = capture_diag(db_, api, rc);
  if (last_diag_) *last_diag_ = d;
  return make_db_error(code, prefix, d);
}

Statement::~Statement() {
  std::unique_lock<std::recursive_mutex> _lk; if (mu_) _lk = std::unique_lock<std::recursive_mutex>(*mu_);
  if (stmt_ != nullptr) sqlite3_finalize(stmt_);
}

Result<void> Statement::bind_int(int idx, std::int64_t value) {
  std::unique_lock<std::recursive_mutex> _lk; if (mu_) _lk = std::unique_lock<std::recursive_mutex>(*mu_);
  const int rc = sqlite3_bind_int64(stmt_, idx, static_cast<sqlite3_int64>(value));
  if (rc != SQLITE_OK) return capture_error(ErrorCode::kIoError, "sqlite bind_int", "sqlite3_bind_int64", rc);
  return Result<void>{};
}

Result<void> Statement::bind_u32(int idx, std::uint32_t value) {
  return bind_int(idx, static_cast<std::int64_t>(value));
}

Result<void> Statement::bind_u64(int idx, std::uint64_t value) {
  // SQLite stores int64 signed; values above INT64_MAX are stored as int64 bit pattern via bind_int64.
  return bind_int(idx, static_cast<std::int64_t>(value));
}

Result<void> Statement::bind_text(int idx, std::string_view value) {
  std::unique_lock<std::recursive_mutex> _lk; if (mu_) _lk = std::unique_lock<std::recursive_mutex>(*mu_);
  const int rc = sqlite3_bind_text(stmt_, idx, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) return capture_error(ErrorCode::kIoError, "sqlite bind_text", "sqlite3_bind_text", rc);
  return Result<void>{};
}

Result<void> Statement::bind_blob(int idx, std::span<const std::uint8_t> value) {
  std::unique_lock<std::recursive_mutex> _lk; if (mu_) _lk = std::unique_lock<std::recursive_mutex>(*mu_);
  if (value.empty()) {
    const int rc = sqlite3_bind_zeroblob(stmt_, idx, 0);
    if (rc != SQLITE_OK) return capture_error(ErrorCode::kIoError, "sqlite bind_blob", "sqlite3_bind_zeroblob", rc);
    return Result<void>{};
  }
  const int rc = sqlite3_bind_blob(stmt_, idx, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) return capture_error(ErrorCode::kIoError, "sqlite bind_blob", "sqlite3_bind_blob", rc);
  return Result<void>{};
}

Result<void> Statement::bind_null(int idx) {
  std::unique_lock<std::recursive_mutex> _lk; if (mu_) _lk = std::unique_lock<std::recursive_mutex>(*mu_);
  const int rc = sqlite3_bind_null(stmt_, idx);
  if (rc != SQLITE_OK) return capture_error(ErrorCode::kIoError, "sqlite bind_null", "sqlite3_bind_null", rc);
  return Result<void>{};
}

Result<bool> Statement::step() {
  std::unique_lock<std::recursive_mutex> _lk; if (mu_) _lk = std::unique_lock<std::recursive_mutex>(*mu_);
  const int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) return true;
  if (rc == SQLITE_DONE) return false;
  return capture_error(ErrorCode::kIoError, "sqlite step", "sqlite3_step", rc);
}

Result<void> Statement::execute() {
  auto r = step();
  if (!r.ok()) return r.error();
  if (r.value()) return MakeError(ErrorCode::kIoError, "sqlite execute returned unexpected row");
  return Result<void>{};
}

void Statement::reset() {
  if (stmt_ != nullptr) {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
  }
}

std::int64_t Statement::column_int64(int idx) const {
  std::unique_lock<std::recursive_mutex> _lk; if (mu_) _lk = std::unique_lock<std::recursive_mutex>(*mu_);
  return static_cast<std::int64_t>(sqlite3_column_int64(stmt_, idx));
}

std::uint32_t Statement::column_u32(int idx) const {
  std::unique_lock<std::recursive_mutex> _lk; if (mu_) _lk = std::unique_lock<std::recursive_mutex>(*mu_);
  return static_cast<std::uint32_t>(sqlite3_column_int64(stmt_, idx));
}

std::uint64_t Statement::column_u64(int idx) const {
  std::unique_lock<std::recursive_mutex> _lk; if (mu_) _lk = std::unique_lock<std::recursive_mutex>(*mu_);
  return static_cast<std::uint64_t>(sqlite3_column_int64(stmt_, idx));
}

std::string Statement::column_text(int idx) const {
  std::unique_lock<std::recursive_mutex> _lk; if (mu_) _lk = std::unique_lock<std::recursive_mutex>(*mu_);
  const auto* text = sqlite3_column_text(stmt_, idx);
  const int len = sqlite3_column_bytes(stmt_, idx);
  if (text == nullptr || len <= 0) return {};
  return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(len));
}

std::vector<std::uint8_t> Statement::column_blob(int idx) const {
  std::unique_lock<std::recursive_mutex> _lk; if (mu_) _lk = std::unique_lock<std::recursive_mutex>(*mu_);
  const auto* data = sqlite3_column_blob(stmt_, idx);
  const int len = sqlite3_column_bytes(stmt_, idx);
  if (data == nullptr || len <= 0) return {};
  const auto* b = static_cast<const std::uint8_t*>(data);
  return std::vector<std::uint8_t>(b, b + len);
}

bool Statement::column_is_null(int idx) const {
  std::unique_lock<std::recursive_mutex> _lk; if (mu_) _lk = std::unique_lock<std::recursive_mutex>(*mu_);
  return sqlite3_column_type(stmt_, idx) == SQLITE_NULL;
}

SqliteDb::~SqliteDb() { (void)close(); }

SqliteDb::SqliteDb(SqliteDb&& other) noexcept
    : db_(other.db_), last_diag_(other.last_diag_), connection_id_(other.connection_id_),
      open_attempt_id_(other.open_attempt_id_), lifecycle_generation_(other.lifecycle_generation_), role_(std::move(other.role_)) {
  other.db_ = nullptr;
  other.connection_id_ = 0;
  other.open_attempt_id_ = 0;
  other.lifecycle_generation_ = 0;
}

SqliteDb& SqliteDb::operator=(SqliteDb&& other) noexcept {
  if (this == &other) return *this;
  (void)close();
  db_ = other.db_;
  last_diag_ = other.last_diag_;
  connection_id_ = other.connection_id_;
  open_attempt_id_ = other.open_attempt_id_;
  lifecycle_generation_ = other.lifecycle_generation_;
  role_ = std::move(other.role_);
  other.db_ = nullptr;
  other.connection_id_ = 0;
  other.open_attempt_id_ = 0;
  other.lifecycle_generation_ = 0;
  return *this;
}

Result<SqliteDb> SqliteDb::open(const std::filesystem::path& path) {
  const std::uint64_t conn_id = ++g_conn_seq;
  const std::uint64_t attempt_id = ++g_open_attempt_seq;
  const std::uint64_t generation = 1;
  const auto t0 = std::chrono::steady_clock::now();
  auto us=[&](){ return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t0).count(); };
  emit_conn_event_global(conn_id, attempt_id, generation, "rw", "OPEN_ATTEMPT", "sqlite3_open_v2", 0, "sqlite3_open_v2", true, nullptr, us());
  sqlite3* db = nullptr;
  const int rc = sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
  if (rc != SQLITE_OK) {
    SqliteDiag d = capture_diag(db, "sqlite3_open_v2", rc);
    emit_conn_event_global(conn_id, attempt_id, generation, "rw", "OPEN_FAILURE", "sqlite3_open_v2", 0, "sqlite3_open_v2", false, &d, us());
    if (db != nullptr) sqlite3_close(db);
    return make_db_error(ErrorCode::kIoError, "sqlite open failed", d);
  }
  emit_conn_event_global(conn_id, attempt_id, generation, "rw", "OPEN_SUCCESS", "sqlite3_open_v2", 0, "sqlite3_open_v2", true, nullptr, us());
  SqliteDb wrapper(db);
  wrapper.connection_id_ = conn_id;
  wrapper.open_attempt_id_ = attempt_id;
  wrapper.lifecycle_generation_ = generation;
  wrapper.role_ = "rw";
  emit_conn_event_global(conn_id, attempt_id, generation, "rw", "INITIALIZATION_BEGIN", "init", 0, "pragma", true, nullptr, us());

  // Alternative F (TB-001): enable extended result codes for consistent observability.
  sqlite3_extended_result_codes(db, 1);

  std::uint32_t seq = 0;
  auto exec_pragma = [&](const char* name, const char* sql) -> Result<void> {
    ++seq;
    emit_conn_event_global(conn_id, attempt_id, generation, "rw", "INITIALIZATION_OPERATION_BEGIN", name, seq, "sqlite3_exec", true, nullptr, us());
    auto pr = wrapper.exec(sql);
    if (!pr.ok()) {
      const auto d = wrapper.last_diag();
      emit_conn_event_global(conn_id, attempt_id, generation, "rw", "INITIALIZATION_OPERATION_FAILURE", name, seq, "sqlite3_exec", false, &d, us());
      emit_conn_event_global(conn_id, attempt_id, generation, "rw", "INITIALIZATION_FAILURE", name, seq, "sqlite3_exec", false, &d, us());
      return pr;
    }
    emit_conn_event_global(conn_id, attempt_id, generation, "rw", "INITIALIZATION_OPERATION_SUCCESS", name, seq, "sqlite3_exec", true, nullptr, us());
    return Result<void>{};
  };

  // 1. busy_timeout FIRST, so it is active before WAL initialization contention.
  {
    auto pr = exec_pragma("PRAGMA busy_timeout=5000", "PRAGMA busy_timeout=5000;");
    if (!pr.ok()) { (void)wrapper.close(); return pr.error(); }
  }
  // 2. journal_mode=WAL with bounded retry (TB-001 Alternative F).
  {
    bool wal_ok = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
      ++seq;
      emit_conn_event_global(conn_id, attempt_id, generation, "rw", "INITIALIZATION_OPERATION_BEGIN", "PRAGMA journal_mode=WAL", seq, "sqlite3_exec", true, nullptr, us());
      auto pr = wrapper.exec("PRAGMA journal_mode=WAL;");
      if (pr.ok()) {
        wal_ok = true;
        emit_conn_event_global(conn_id, attempt_id, generation, "rw", "INITIALIZATION_OPERATION_SUCCESS", "PRAGMA journal_mode=WAL", seq, "sqlite3_exec", true, nullptr, us());
        break;
      }
      const auto d = wrapper.last_diag();
      emit_conn_event_global(conn_id, attempt_id, generation, "rw", "INITIALIZATION_OPERATION_FAILURE", "PRAGMA journal_mode=WAL", seq, "sqlite3_exec", false, &d, us());
      if ((d.primary == 5 || d.extended == 261) && attempt < 2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      emit_conn_event_global(conn_id, attempt_id, generation, "rw", "INITIALIZATION_FAILURE", "PRAGMA journal_mode=WAL", seq, "sqlite3_exec", false, &d, us());
      (void)wrapper.close();
      return pr.error();
    }
    if (!wal_ok) {
      emit_conn_event_global(conn_id, attempt_id, generation, "rw", "INITIALIZATION_FAILURE", "PRAGMA journal_mode=WAL", seq, "sqlite3_exec", false, nullptr, us());
      (void)wrapper.close();
      return MakeError(ErrorCode::kIoError, "sqlite open failed: journal_mode=WAL retry exhausted");
    }
  }
  // 3. Remaining pragmas.
  const std::pair<const char*, const char*> rest[] = {
      {"PRAGMA synchronous=FULL", "PRAGMA synchronous=FULL;"},
      {"PRAGMA foreign_keys=ON", "PRAGMA foreign_keys=ON;"},
      {"PRAGMA temp_store=MEMORY", "PRAGMA temp_store=MEMORY;"},
  };
  for (const auto& [name, sql] : rest) {
    auto pr = exec_pragma(name, sql);
    if (!pr.ok()) { (void)wrapper.close(); return pr.error(); }
  }
  emit_conn_event_global(conn_id, attempt_id, generation, "rw", "INITIALIZATION_SUCCESS", "init", seq, "sqlite3_exec", true, nullptr, us());
  emit_conn_event_global(conn_id, attempt_id, generation, "rw", "CONNECTION_READY", "init", seq, "sqlite3_open_v2", true, nullptr, us());
  return wrapper;
}

Result<SqliteDb> SqliteDb::open_readonly(const std::filesystem::path& path) {
  const std::uint64_t conn_id = ++g_conn_seq;
  const std::uint64_t attempt_id = ++g_open_attempt_seq;
  const std::uint64_t generation = 1;
  const auto t0 = std::chrono::steady_clock::now();
  auto us=[&](){ return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t0).count(); };
  emit_conn_event_global(conn_id, attempt_id, generation, "ro", "OPEN_ATTEMPT", "sqlite3_open_v2", 0, "sqlite3_open_v2", true, nullptr, us());
  sqlite3* db = nullptr;
  const int rc = sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr);
  if (rc != SQLITE_OK) {
    SqliteDiag d = capture_diag(db, "sqlite3_open_v2", rc);
    emit_conn_event_global(conn_id, attempt_id, generation, "ro", "OPEN_FAILURE", "sqlite3_open_v2", 0, "sqlite3_open_v2", false, &d, us());
    if (db != nullptr) sqlite3_close(db);
    return make_db_error(ErrorCode::kIoError, "sqlite open readonly failed", d);
  }
  emit_conn_event_global(conn_id, attempt_id, generation, "ro", "OPEN_SUCCESS", "sqlite3_open_v2", 0, "sqlite3_open_v2", true, nullptr, us());
  SqliteDb wrapper(db);
  wrapper.connection_id_ = conn_id;
  wrapper.open_attempt_id_ = attempt_id;
  wrapper.lifecycle_generation_ = generation;
  wrapper.role_ = "ro";
  emit_conn_event_global(conn_id, attempt_id, generation, "ro", "INITIALIZATION_BEGIN", "init", 0, "pragma", true, nullptr, us());
  const std::pair<const char*, const char*> pragmas[] = {
      {"PRAGMA temp_store=MEMORY", "PRAGMA temp_store=MEMORY;"},
      {"PRAGMA busy_timeout=2000", "PRAGMA busy_timeout=2000;"},
  };
  std::uint32_t seq = 0;
  for (const auto& [name, sql] : pragmas) {
    ++seq;
    emit_conn_event_global(conn_id, attempt_id, generation, "ro", "INITIALIZATION_OPERATION_BEGIN", name, seq, "sqlite3_exec", true, nullptr, us());
    auto pr = wrapper.exec(sql);
    if (!pr.ok()) {
      const auto d = wrapper.last_diag();
      emit_conn_event_global(conn_id, attempt_id, generation, "ro", "INITIALIZATION_OPERATION_FAILURE", name, seq, "sqlite3_exec", false, &d, us());
      emit_conn_event_global(conn_id, attempt_id, generation, "ro", "INITIALIZATION_FAILURE", name, seq, "sqlite3_exec", false, &d, us());
      (void)wrapper.close();
      return pr.error();
    }
    emit_conn_event_global(conn_id, attempt_id, generation, "ro", "INITIALIZATION_OPERATION_SUCCESS", name, seq, "sqlite3_exec", true, nullptr, us());
  }
  emit_conn_event_global(conn_id, attempt_id, generation, "ro", "INITIALIZATION_SUCCESS", "init", seq, "sqlite3_exec", true, nullptr, us());
  emit_conn_event_global(conn_id, attempt_id, generation, "ro", "CONNECTION_READY", "init", seq, "sqlite3_open_v2", true, nullptr, us());
  return wrapper;
}

Error SqliteDb::capture_error_locked(ErrorCode code, std::string_view prefix, std::string_view api, int rc) const {
  last_diag_ = capture_diag(db_, api, rc);
  return make_db_error(code, prefix, last_diag_);
}

Result<void> SqliteDb::close() {
  std::scoped_lock _lk(mu_);
  if (db_ == nullptr) return Result<void>{};
  emit_conn_event("CLOSE_ATTEMPT", "sqlite3_close", true, nullptr, 0);
  const int rc = sqlite3_close(db_);
  if (rc != SQLITE_OK) {
    auto e = capture_error_locked(ErrorCode::kIoError, "sqlite close", "sqlite3_close", rc);
    const auto d = last_diag_;
    emit_conn_event("CLOSE_FAILURE", "sqlite3_close", false, &d, 0);
    return e;
  }
  db_ = nullptr;
  emit_conn_event("CLOSE_SUCCESS", "sqlite3_close", true, nullptr, 0);
  return Result<void>{};
}

Result<void> SqliteDb::exec(std::string_view sql) {
  std::scoped_lock _lk(mu_);
  char* err = nullptr;
  const int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    // Capture error codes immediately after exec, before freeing err/other calls.
    last_diag_ = capture_diag(db_, "sqlite3_exec", rc);
    if (err != nullptr) { last_diag_.message = err; sqlite3_free(err); }
    return make_db_error(ErrorCode::kIoError, "sqlite exec", last_diag_);
  }
  return Result<void>{};
}

Result<Statement> SqliteDb::prepare(std::string_view sql) {
  std::scoped_lock _lk(mu_);
  sqlite3_stmt* stmt = nullptr;
  const int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
  if (rc != SQLITE_OK) return capture_error_locked(ErrorCode::kIoError, "sqlite prepare", "sqlite3_prepare_v2", rc);
  return Statement(db_, stmt, &mu_, &last_diag_);
}

Result<void> SqliteDb::begin() { return exec("BEGIN IMMEDIATE TRANSACTION;"); }
Result<void> SqliteDb::commit() { return exec("COMMIT;"); }
Result<void> SqliteDb::rollback() { return exec("ROLLBACK;"); }

std::int64_t SqliteDb::last_insert_rowid() const { return static_cast<std::int64_t>(sqlite3_last_insert_rowid(db_)); }

Result<void> SqliteDb::backup_to(const std::filesystem::path& path) {
  std::scoped_lock _lk(mu_);
  sqlite3* dest = nullptr;
  int rc = sqlite3_open_v2(path.string().c_str(), &dest, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  if (rc != SQLITE_OK) {
    std::string msg = "sqlite backup open dest failed";
    if (dest != nullptr) { msg += ": "; msg += sqlite3_errmsg(dest); sqlite3_close(dest); }
    return MakeError(ErrorCode::kIoError, std::move(msg));
  }
  sqlite3_backup* backup = sqlite3_backup_init(dest, "main", db_, "main");
  if (backup == nullptr) {
    const SqliteDiag d = capture_diag(dest, "sqlite3_backup_init", SQLITE_ERROR);
    sqlite3_close(dest);
    return make_db_error(ErrorCode::kIoError, "sqlite backup init failed", d);
  }
  rc = sqlite3_backup_step(backup, -1);
  if (rc != SQLITE_DONE) {
    const SqliteDiag d = capture_diag(dest, "sqlite3_backup_step", rc);
    sqlite3_backup_finish(backup);
    sqlite3_close(dest);
    return make_db_error(ErrorCode::kIoError, "sqlite backup step failed", d);
  }
  sqlite3_backup_finish(backup);
  sqlite3_close(dest);
  return Result<void>{};
}

Result<void> SqliteDb::integrity_check() const {
  std::scoped_lock _lk(mu_);
  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, "PRAGMA integrity_check;", -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return capture_error_locked(ErrorCode::kIoError, "sqlite integrity_check prepare", "sqlite3_prepare_v2", rc);
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) { auto e = capture_error_locked(ErrorCode::kIoError, "sqlite integrity_check step", "sqlite3_step", rc); sqlite3_finalize(stmt); return e; }
  const auto* text = sqlite3_column_text(stmt, 0);
  std::string result = text ? reinterpret_cast<const char*>(text) : "";
  sqlite3_finalize(stmt);
  if (result != "ok") return MakeError(ErrorCode::kIoError, "sqlite integrity_check failed: " + result);
  return Result<void>{};
}

std::string SqliteDb::last_error() const {
  std::scoped_lock _lk(mu_);
  return db_ ? sqlite3_errmsg(db_) : "";
}

TransactionGuard::~TransactionGuard() {
  if (active_ && !committed_) {
    (void)db_->rollback();
  }
}

Result<void> TransactionGuard::begin() {
  auto r = db_->begin();
  if (!r.ok()) return r;
  active_ = true;
  committed_ = false;
  return Result<void>{};
}

Result<void> TransactionGuard::commit() {
  auto r = db_->commit();
  if (!r.ok()) return r;
  committed_ = true;
  active_ = false;
  return Result<void>{};
}

void TransactionGuard::rollback() {
  if (active_ && !committed_) {
    (void)db_->rollback();
    active_ = false;
  }
}

}  // namespace phantomx::db
