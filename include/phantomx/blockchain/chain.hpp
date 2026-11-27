#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/database/sqlite_db.hpp"
#include "phantomx/primitives/block.hpp"
#include "phantomx/utxo/utxo.hpp"

namespace phantomx::blockchain {

struct BlockIndexEntry {
  Hash256Bytes hash{};
  Hash256Bytes prev_hash{};
  std::uint32_t height = 0;
  std::uint64_t timestamp = 0;
  bool has_block_data = false;
  bool active = false; // on active/finalized chain path
};

struct ChainTip {
  Hash256Bytes hash{};
  std::uint32_t height = 0;
};

struct SnapshotMeta {
  std::string id;
  Hash256Bytes tip_hash{};
  std::uint32_t tip_height = 0;
  std::uint64_t created_at = 0;
  std::string path;
};

struct UndoEntry {
  OutPoint outpoint;
  UtxoEntry utxo;
};

struct BlockUndoData {
  Hash256Bytes block_hash{};
  std::vector<UndoEntry> spent;
};

class ChainDatabase {
 public:
  ChainDatabase() = default;
  ~ChainDatabase() = default;
  ChainDatabase(ChainDatabase&&) noexcept = default;
  ChainDatabase& operator=(ChainDatabase&&) noexcept = default;
  ChainDatabase(const ChainDatabase&) = delete;
  ChainDatabase& operator=(const ChainDatabase&) = delete;

  [[nodiscard]] static Result<ChainDatabase> open(const std::filesystem::path& path);
  [[nodiscard]] Result<void> close();

  // Block store
  [[nodiscard]] Result<void> write_block(const Block& block);
  [[nodiscard]] Result<Block> read_block(const Hash256Bytes& hash) const;
  [[nodiscard]] Result<std::optional<BlockIndexEntry>> get_block_index(const Hash256Bytes& hash) const;
  [[nodiscard]] Result<std::optional<Hash256Bytes>> get_hash_by_height(std::uint32_t height) const;

  // Validation/apply/disconnect
  [[nodiscard]] Result<void> validate_block(const Block& block) const;
  [[nodiscard]] Result<void> apply_block(const Block& block);
  [[nodiscard]] Result<void> disconnect_block(const Hash256Bytes& hash, bool allow_finalized = false);

  // State
  [[nodiscard]] Result<std::optional<ChainTip>> tip() const;
  [[nodiscard]] Result<UtxoSet> load_utxo_set() const;
  [[nodiscard]] Result<std::optional<UtxoEntry>> get_utxo(const OutPoint& outpoint) const;

  // Maintenance
  [[nodiscard]] Result<void> recover_after_crash();
  [[nodiscard]] Result<void> verify_database() const;
  [[nodiscard]] Result<void> backup(const std::filesystem::path& backup_path) const;
  [[nodiscard]] Result<void> restore(const std::filesystem::path& backup_path);
  [[nodiscard]] Result<SnapshotMeta> create_snapshot(const std::filesystem::path& snapshot_path) const;
  [[nodiscard]] Result<void> verify_snapshot(const std::filesystem::path& snapshot_path) const;

  // Metadata
  [[nodiscard]] Result<void> set_metadata(std::string_view key, std::string_view value);
  [[nodiscard]] Result<std::optional<std::string>> get_metadata(std::string_view key) const;

  // Verifier registration challenges (atomic single-use).
  struct VerifierChallengeRecord {
    std::string challenge_id;
    std::string verifier_id;
    std::string pubkey_hex;
    std::string owner;
    std::string network;
    std::vector<std::uint8_t> payload_blob;
    std::uint64_t issued_at{0};
    std::uint64_t expires_at{0};
    std::optional<std::uint64_t> used_at;
    std::string status;
  };

  struct QuotaTxDiagEvent {
    std::uint64_t operation_id{0};
    std::uint64_t transaction_id{0};
    std::uint64_t connection_id{0};
    std::uint64_t process_id{0};
    std::uint64_t thread_id{0};
    std::uint32_t retry_attempt{0};
    std::string stage;
    std::string sqlite_api;
    bool ok{false};
    int sqlite_primary{0};
    int sqlite_extended{0};
    int sqlite_rc{0};
    std::string sqlite_message;
    std::int64_t elapsed_us{0};
    bool tx_active{false};
    bool rollback_attempted{false};
    bool rollback_ok{false};
    bool has_original_error{false};
    int original_primary{0};
    int original_extended{0};
    int original_rc{0};
    std::string original_api;
    std::string original_message;
    bool rollback_error_present{false};
    int rollback_primary{0};
    int rollback_extended{0};
    int rollback_rc{0};
    std::string rollback_api;
    std::string rollback_message;
    std::string final_result;
  };
  using QuotaTxDiagObserver = std::function<void(const QuotaTxDiagEvent&)>;
  void set_quota_tx_observer(QuotaTxDiagObserver obs, bool enabled) {
    std::scoped_lock lk(*mu_);
    quota_obs_ = std::move(obs);
    quota_obs_enabled_ = enabled;
  }
  void clear_quota_tx_observer() {
    std::scoped_lock lk(*mu_);
    quota_obs_ = nullptr;
    quota_obs_enabled_ = false;
  }
  [[nodiscard]] Result<void> put_verifier_challenge(const VerifierChallengeRecord& r);
  // Atomic quota-enforced insert (Phase 15C.8): cleanup + global/per-identity quota + insert in one BEGIN IMMEDIATE tx.
  // Returns ErrorCode::kResourceExhausted-equivalent via kInternalError message? No - use specific error codes in Error.
  [[nodiscard]] Result<void> put_verifier_challenge_with_quota(const VerifierChallengeRecord& r,
                                                               std::uint64_t now_ts,
                                                               std::uint64_t global_cap,
                                                               std::uint64_t per_identity_cap,
                                                               std::uint64_t consumed_retention_sec,
                                                               std::uint64_t cleanup_batch);
  [[nodiscard]] Result<std::optional<VerifierChallengeRecord>> get_verifier_challenge(const std::string& challenge_id) const;
  [[nodiscard]] Result<bool> consume_verifier_challenge(const std::string& challenge_id,
                                                        const std::string& verifier_id,
                                                        const std::string& pubkey_hex,
                                                        const std::string& owner,
                                                        const std::string& network,
                                                        std::uint64_t now_ts);
  [[nodiscard]] Result<std::uint64_t> count_active_verifier_challenges(std::uint64_t now_ts) const;
  [[nodiscard]] Result<std::uint64_t> count_active_verifier_challenges_for(std::string_view verifier_id, std::string_view owner, std::uint64_t now_ts) const;
  [[nodiscard]] Result<void> cleanup_verifier_challenges(std::uint64_t now_ts, std::uint64_t consumed_retention_sec, std::uint64_t max_delete);

  [[nodiscard]] db::SqliteDb& db() noexcept { return db_; }
  [[nodiscard]] const db::SqliteDb& db() const noexcept { return db_; }

 private:
  [[nodiscard]] Result<void> init_schema();
  [[nodiscard]] Result<void> apply_genesis_if_needed();
  [[nodiscard]] Result<BlockUndoData> build_undo(const Block& block) const;

  mutable std::unique_ptr<std::recursive_mutex> mu_{std::make_unique<std::recursive_mutex>()};
  db::SqliteDb db_;
  std::filesystem::path path_;
  mutable std::uint64_t quota_conn_seq_{0};
  std::uint64_t quota_conn_id_{0};
  QuotaTxDiagObserver quota_obs_{};
  bool quota_obs_enabled_{false};
};

}  // namespace phantomx::blockchain
