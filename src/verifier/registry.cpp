#include "phantomx/verifier/registry.hpp"

#include "phantomx/primitives/hash.hpp"

namespace phantomx::verifier {

std::string VerifierRegistry::key(const std::array<std::uint8_t, 32>& pk) {
  return to_hex(std::span<const std::uint8_t>(pk.data(), pk.size()));
}

Result<void> VerifierRegistry::register_verifier(VerifierRecord v, std::uint64_t now) {
  if (v.verifier_pubkey == std::array<std::uint8_t, 32>{}) return MakeError(ErrorCode::kInvalidArgument, "empty verifier pubkey");
  const auto k = key(v.verifier_pubkey);
  if (records_.find(k) != records_.end()) return MakeError(ErrorCode::kInvalidArgument, "verifier already registered");
  v.registered_at = now;
  v.last_seen = now;
  if (v.status == VerifierStatus::kBanned) return MakeError(ErrorCode::kInvalidArgument, "cannot register banned verifier");
  records_[k] = std::move(v);
  return Result<void>{};
}

Result<void> VerifierRegistry::mark_offline(const std::array<std::uint8_t, 32>& verifier_pubkey, std::uint64_t now) {
  auto it = records_.find(key(verifier_pubkey));
  if (it == records_.end()) return MakeError(ErrorCode::kNotFound, "verifier not found");
  it->second.status = VerifierStatus::kOffline;
  it->second.last_seen = now;
  return Result<void>{};
}

Result<void> VerifierRegistry::mark_active(const std::array<std::uint8_t, 32>& verifier_pubkey, std::uint64_t now) {
  auto it = records_.find(key(verifier_pubkey));
  if (it == records_.end()) return MakeError(ErrorCode::kNotFound, "verifier not found");
  if (it->second.status == VerifierStatus::kBanned) return MakeError(ErrorCode::kInvalidArgument, "banned verifier cannot be active");
  it->second.status = VerifierStatus::kActive;
  it->second.last_seen = now;
  return Result<void>{};
}

Result<void> VerifierRegistry::suspend(const std::array<std::uint8_t, 32>& verifier_pubkey, std::string /*reason*/) {
  auto it = records_.find(key(verifier_pubkey));
  if (it == records_.end()) return MakeError(ErrorCode::kNotFound, "verifier not found");
  it->second.status = VerifierStatus::kSuspended;
  return Result<void>{};
}

Result<void> VerifierRegistry::update_reputation(const std::array<std::uint8_t, 32>& verifier_pubkey, std::int64_t delta) {
  auto it = records_.find(key(verifier_pubkey));
  if (it == records_.end()) return MakeError(ErrorCode::kNotFound, "verifier not found");
  it->second.reputation += delta;
  if (delta < 0) it->second.jobs_failed++;
  else if (delta > 0) it->second.jobs_completed++;
  return Result<void>{};
}

Result<void> VerifierRegistry::account_reward(const std::array<std::uint8_t, 32>& verifier_pubkey, std::uint64_t atoms) {
  auto it = records_.find(key(verifier_pubkey));
  if (it == records_.end()) return MakeError(ErrorCode::kNotFound, "verifier not found");
  it->second.reward_atoms_accounted += atoms;
  return Result<void>{};
}

const VerifierRecord* VerifierRegistry::find(const std::array<std::uint8_t, 32>& verifier_pubkey) const {
  auto it = records_.find(key(verifier_pubkey));
  if (it == records_.end()) return nullptr;
  return &it->second;
}

VerifierEligibility VerifierRegistry::eligible_for(const std::array<std::uint8_t, 32>& verifier_pubkey, VerifierType required) const {
  const auto* v = find(verifier_pubkey);
  if (v == nullptr) return {false, "not registered"};
  if (v->status != VerifierStatus::kActive) return {false, "verifier not active"};
  if (v->type != required) return {false, "verifier type mismatch"};
  if (v->reputation < -100) return {false, "reputation too low"};
  return {true, ""};
}

std::vector<VerifierRecord> VerifierRegistry::active_verifiers(VerifierType required) const {
  std::vector<VerifierRecord> out;
  for (const auto& [_, v] : records_) {
    auto e = eligible_for(v.verifier_pubkey, required);
    if (e.eligible) out.push_back(v);
  }
  return out;
}

}  // namespace phantomx::verifier
