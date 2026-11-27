#include "phantomx/pool/pool.hpp"

#include <algorithm>

#include "phantomx/primitives/hash.hpp"

namespace phantomx::pool {
namespace {
bool is_zero(const std::array<std::uint8_t, 32>& id) {
  return std::all_of(id.begin(), id.end(), [](std::uint8_t b) { return b == 0; });
}
}  // namespace

std::string PoolLedger::worker_key(const std::array<std::uint8_t, 32>& id) {
  return to_hex(std::span<const std::uint8_t>(id.data(), id.size()));
}

std::string PoolLedger::payout_key(const std::array<std::uint8_t, 32>& worker_id, std::uint64_t epoch, const std::string& category) {
  return worker_key(worker_id) + ":" + std::to_string(epoch) + ":" + category;
}

Result<void> PoolLedger::register_worker(WorkerRecord w, std::uint64_t now) {
  if (is_zero(w.worker_id)) return MakeError(ErrorCode::kInvalidArgument, "worker_id empty");
  if (is_zero(w.operator_id)) return MakeError(ErrorCode::kInvalidArgument, "operator_id empty");
  const auto k = worker_key(w.worker_id);
  if (worker_index_.find(k) != worker_index_.end()) return MakeError(ErrorCode::kInvalidArgument, "worker already registered");
  if (w.auth_token_hash.empty()) return MakeError(ErrorCode::kInvalidArgument, "auth_token_hash required");
  w.registered_at = now;
  w.last_seen = now;
  worker_index_[k] = workers_.size();
  workers_.push_back(std::move(w));
  return Result<void>{};
}

Result<void> PoolLedger::authenticate(const std::array<std::uint8_t, 32>& worker_id, const std::string& auth_token_hash) const {
  auto it = worker_index_.find(worker_key(worker_id));
  if (it == worker_index_.end()) return MakeError(ErrorCode::kNotFound, "worker not found");
  const auto& w = workers_[it->second];
  if (!w.active) return MakeError(ErrorCode::kInvalidArgument, "worker inactive");
  if (w.auth_token_hash != auth_token_hash) return MakeError(ErrorCode::kInvalidArgument, "authentication failed");
  return Result<void>{};
}

Result<void> PoolLedger::submit_share(const ShareRecord& share) {
  if (is_zero(share.worker_id)) return MakeError(ErrorCode::kInvalidArgument, "worker_id empty");
  auto it = worker_index_.find(worker_key(share.worker_id));
  if (it == worker_index_.end()) return MakeError(ErrorCode::kNotFound, "worker not found");
  const auto& w = workers_[it->second];
  if (!w.active) return MakeError(ErrorCode::kInvalidArgument, "worker inactive");
  if (w.role != share.role) return MakeError(ErrorCode::kInvalidArgument, "role mismatch");
  if (share.units == 0) return MakeError(ErrorCode::kInvalidArgument, "share units must be > 0");
  if (share.evidence_hash_hex.empty()) return MakeError(ErrorCode::kInvalidArgument, "evidence hash required");

  // No duplicate shares: same worker+epoch+role+evidence hash rejected
  for (const auto& s : shares_) {
    if (s.epoch == share.epoch && s.worker_id == share.worker_id && s.role == share.role && s.evidence_hash_hex == share.evidence_hash_hex) {
      return MakeError(ErrorCode::kInvalidArgument, "duplicate share");
    }
  }
  shares_.push_back(share);
  return Result<void>{};
}

Result<void> PoolLedger::record_payout(PayoutRecord payout, std::uint64_t finalized_epoch) {
  if (is_zero(payout.worker_id)) return MakeError(ErrorCode::kInvalidArgument, "worker_id empty");
  if (payout.amount_atoms == 0) return MakeError(ErrorCode::kInvalidArgument, "payout amount must be > 0");
  if (payout.epoch > finalized_epoch) {
    return MakeError(ErrorCode::kInvalidArgument, "payout epoch not finalized");
  }
  const auto k = payout_key(payout.worker_id, payout.epoch, payout.category);
  if (payout_index_.find(k) != payout_index_.end()) return MakeError(ErrorCode::kInvalidArgument, "duplicate payout");
  payout.paid = true;
  payout_index_[k] = true;
  payouts_.push_back(std::move(payout));
  return Result<void>{};
}

bool PoolLedger::has_payout(const std::array<std::uint8_t, 32>& worker_id, std::uint64_t epoch, const std::string& category) const {
  return payout_index_.find(payout_key(worker_id, epoch, category)) != payout_index_.end();
}

std::vector<rewards::EpochParticipant> PoolLedger::participants_for_role(PoolRole role, std::uint64_t epoch) const {
  std::unordered_map<std::string, rewards::EpochParticipant> agg;
  for (const auto& s : shares_) {
    if (s.epoch != epoch || s.role != role) continue;
    auto k = worker_key(s.worker_id);
    auto& p = agg[k];
    p.id = s.worker_id;
    p.work_units += s.units;
  }
  std::vector<rewards::EpochParticipant> out;
  out.reserve(agg.size());
  for (auto& [_, p] : agg) out.push_back(std::move(p));
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
  return out;
}

std::uint64_t PoolLedger::estimate_reward_atoms(const std::array<std::uint8_t, 32>& worker_id, PoolRole role, std::uint64_t epoch, std::uint64_t category_atoms) const {
  auto parts = participants_for_role(role, epoch);
  std::uint64_t total = 0;
  std::uint64_t mine = 0;
  for (const auto& p : parts) {
    total += p.work_units;
    if (p.id == worker_id) mine = p.work_units;
  }
  if (total == 0 || mine == 0) return 0;
  // integer floor estimate
#if defined(__SIZEOF_INT128__)
  return static_cast<std::uint64_t>((static_cast<__uint128_t>(category_atoms) * static_cast<__uint128_t>(mine)) / static_cast<__uint128_t>(total));
#else
  const std::uint64_t q = category_atoms / total;
  const std::uint64_t r = category_atoms % total;
  return q * mine + (r * mine) / total;
#endif
}

PoolDashboard PoolLedger::pool_dashboard() const {
  PoolDashboard d;
  d.workers = workers_.size();
  for (const auto& w : workers_) if (w.active) ++d.active_workers;
  for (const auto& s : shares_) d.total_shares += s.units;
  for (const auto& p : payouts_) if (p.paid) d.total_paid_atoms += p.amount_atoms;
  return d;
}

WorkerDashboard PoolLedger::worker_dashboard(const std::array<std::uint8_t, 32>& worker_id, std::uint64_t epoch) const {
  WorkerDashboard d;
  d.worker_id = worker_id;
  for (const auto& s : shares_) if (s.epoch == epoch && s.worker_id == worker_id) d.share_units += s.units;
  for (const auto& p : payouts_) if (p.worker_id == worker_id && p.paid) d.paid_atoms += p.amount_atoms;
  return d;
}

}  // namespace phantomx::pool
