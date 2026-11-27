#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/rewards/rewards.hpp"

namespace phantomx::pool {

enum class PoolRole : std::uint8_t {
  kOperator = 1,
  kPoiWorker = 2,
  kVerifierWorker = 3,
};

struct WorkerRecord {
  std::array<std::uint8_t, 32> worker_id{};
  std::array<std::uint8_t, 32> operator_id{}; // pool operator identity
  PoolRole role = PoolRole::kPoiWorker;
  std::string auth_token_hash; // store hash only
  bool active = true;
  std::uint64_t registered_at = 0;
  std::uint64_t last_seen = 0;
};

struct ShareRecord {
  std::array<std::uint8_t, 32> worker_id{};
  PoolRole role = PoolRole::kPoiWorker;
  std::uint64_t epoch = 0;
  std::uint64_t units = 0;
  std::string evidence_hash_hex;
};

struct PayoutRecord {
  std::array<std::uint8_t, 32> worker_id{};
  std::uint64_t epoch = 0;
  std::uint64_t amount_atoms = 0;
  std::string category;
  bool paid = false;
};

struct PoolDashboard {
  std::uint64_t workers = 0;
  std::uint64_t active_workers = 0;
  std::uint64_t total_shares = 0;
  std::uint64_t total_paid_atoms = 0;
};

struct WorkerDashboard {
  std::array<std::uint8_t, 32> worker_id{};
  std::uint64_t share_units = 0;
  std::uint64_t estimated_reward_atoms = 0;
  std::uint64_t paid_atoms = 0;
};

class PoolLedger {
 public:
  [[nodiscard]] Result<void> register_worker(WorkerRecord w, std::uint64_t now);
  [[nodiscard]] Result<void> authenticate(const std::array<std::uint8_t, 32>& worker_id, const std::string& auth_token_hash) const;
  [[nodiscard]] Result<void> submit_share(const ShareRecord& share);
  [[nodiscard]] Result<void> record_payout(PayoutRecord payout, std::uint64_t finalized_epoch = 0);
  [[nodiscard]] bool has_payout(const std::array<std::uint8_t, 32>& worker_id, std::uint64_t epoch, const std::string& category) const;

  [[nodiscard]] std::vector<rewards::EpochParticipant> participants_for_role(PoolRole role, std::uint64_t epoch) const;
  [[nodiscard]] std::uint64_t estimate_reward_atoms(const std::array<std::uint8_t, 32>& worker_id, PoolRole role, std::uint64_t epoch, std::uint64_t category_atoms) const;

  [[nodiscard]] PoolDashboard pool_dashboard() const;
  [[nodiscard]] WorkerDashboard worker_dashboard(const std::array<std::uint8_t, 32>& worker_id, std::uint64_t epoch) const;
  [[nodiscard]] const std::vector<WorkerRecord>& workers() const noexcept { return workers_; }
  [[nodiscard]] const std::vector<ShareRecord>& shares() const noexcept { return shares_; }
  [[nodiscard]] const std::vector<PayoutRecord>& payouts() const noexcept { return payouts_; }

 private:
  static std::string worker_key(const std::array<std::uint8_t, 32>& id);
  static std::string payout_key(const std::array<std::uint8_t, 32>& worker_id, std::uint64_t epoch, const std::string& category);

  std::vector<WorkerRecord> workers_;
  std::vector<ShareRecord> shares_;
  std::vector<PayoutRecord> payouts_;
  std::unordered_map<std::string, std::size_t> worker_index_;
  std::unordered_map<std::string, bool> payout_index_;
};

}  // namespace phantomx::pool
