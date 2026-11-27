#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "phantomx/common/error.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace phantomx::db {

struct SqliteDiag {
  int rc{0};
  int primary{0};
  int extended{0};
  std::string message;
  std::string api;
};

struct SqliteConnectionEvent {
  std::uint64_t monotonic_seq{0};
  std::uint64_t connection_id{0};
  std::uint64_t open_attempt_id{0};
  std::uint64_t lifecycle_generation{0};
  std::uint64_t process_id{0};
  std::uint64_t thread_id{0};
  std::uint64_t operation_id{0};
  std::uint64_t transaction_id{0};
  std::string database_role;
  std::string stage;
  std::string operation_name;
  std::uint32_t operation_seq{0};
  std::string sqlite_api;
  bool ok{false};
  int sqlite_rc{0};
  int sqlite_primary{0};
  int sqlite_extended{0};
  std::string sqlite_message;
  std::int64_t elapsed_us{0};
};

class Statement {
 public:
  Statement() = default;
  Statement(sqlite3* db, sqlite3_stmt* stmt, std::recursive_mutex* mu = nullptr, SqliteDiag* last_diag = nullptr)
      : db_(db), stmt_(stmt), mu_(mu), last_diag_(last_diag) {}
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  Statement(Statement&& other) noexcept;
  Statement& operator=(Statement&& other) noexcept;
  ~Statement();

  [[nodiscard]] Result<void> bind_int(int idx, std::int64_t value);
  [[nodiscard]] Result<void> bind_u32(int idx, std::uint32_t value);
  [[nodiscard]] Result<void> bind_u64(int idx, std::uint64_t value);
  [[nodiscard]] Result<void> bind_text(int idx, std::string_view value);
  [[nodiscard]] Result<void> bind_blob(int idx, std::span<const std::uint8_t> value);
  [[nodiscard]] Result<void> bind_null(int idx);

  // Returns true if a row is available, false on done.
  [[nodiscard]] Result<bool> step();
  [[nodiscard]] Result<void> execute();  // step expecting done
  void reset();

  [[nodiscard]] std::int64_t column_int64(int idx) const;
  [[nodiscard]] std::uint32_t column_u32(int idx) const;
  [[nodiscard]] std::uint64_t column_u64(int idx) const;
  [[nodiscard]] std::string column_text(int idx) const;
  [[nodiscard]] std::vector<std::uint8_t> column_blob(int idx) const;
  [[nodiscard]] bool column_is_null(int idx) const;

  [[nodiscard]] sqlite3_stmt* get() const noexcept { return stmt_; }

 private:
  Error capture_error(ErrorCode code, std::string_view prefix, std::string_view api, int rc);
  sqlite3* db_ = nullptr;
  sqlite3_stmt* stmt_ = nullptr;
  std::recursive_mutex* mu_ = nullptr;
  SqliteDiag* last_diag_ = nullptr;
};

class SqliteDb {
 public:
  using ConnectionObserver = std::function<void(const SqliteConnectionEvent&)>;

  SqliteDb() = default;
  ~SqliteDb();

  SqliteDb(const SqliteDb&) = delete;
  SqliteDb& operator=(const SqliteDb&) = delete;
  SqliteDb(SqliteDb&& other) noexcept;
  SqliteDb& operator=(SqliteDb&& other) noexcept;

  [[nodiscard]] static Result<SqliteDb> open(const std::filesystem::path& path);
  [[nodiscard]] static Result<SqliteDb> open_readonly(const std::filesystem::path& path);
  static void set_connection_observer(ConnectionObserver obs, bool enabled);
  static void clear_connection_observer();

  [[nodiscard]] Result<void> close();
  [[nodiscard]] bool is_open() const noexcept { return db_ != nullptr; }

  [[nodiscard]] Result<void> exec(std::string_view sql);
  [[nodiscard]] Result<Statement> prepare(std::string_view sql);
  [[nodiscard]] Result<void> begin();
  [[nodiscard]] Result<void> commit();
  [[nodiscard]] Result<void> rollback();
  [[nodiscard]] std::int64_t last_insert_rowid() const;

  [[nodiscard]] Result<void> backup_to(const std::filesystem::path& path);
  [[nodiscard]] Result<void> integrity_check() const;
  [[nodiscard]] std::string last_error() const;
  [[nodiscard]] SqliteDiag last_diag() const { std::scoped_lock _lk(mu_); return last_diag_; }
  [[nodiscard]] std::uint64_t connection_id() const { return connection_id_; }
  [[nodiscard]] std::uint64_t open_attempt_id() const { return open_attempt_id_; }
  [[nodiscard]] std::uint64_t lifecycle_generation() const { return lifecycle_generation_; }
  [[nodiscard]] std::string_view database_role() const { return role_; }

  [[nodiscard]] sqlite3* handle() const noexcept { return db_; }

 private:
  explicit SqliteDb(sqlite3* db) : db_(db) {}
  Error capture_error_locked(ErrorCode code, std::string_view prefix, std::string_view api, int rc) const;
  void emit_conn_event(const char* stage, const char* api, bool ok, const SqliteDiag* diag, std::int64_t elapsed_us) const;
  sqlite3* db_ = nullptr;
  mutable std::recursive_mutex mu_;
  mutable SqliteDiag last_diag_{};
  std::uint64_t connection_id_{0};
  std::uint64_t open_attempt_id_{0};
  std::uint64_t lifecycle_generation_{0};
  std::string role_{"rw"};
};

class TransactionGuard {
 public:
  explicit TransactionGuard(SqliteDb& db) : db_(&db) {}
  ~TransactionGuard();
  TransactionGuard(const TransactionGuard&) = delete;
  TransactionGuard& operator=(const TransactionGuard&) = delete;
  [[nodiscard]] Result<void> begin();
  [[nodiscard]] Result<void> commit();
  void rollback();

 private:
  SqliteDb* db_ = nullptr;
  bool active_ = false;
  bool committed_ = false;
};

}  // namespace phantomx::db
