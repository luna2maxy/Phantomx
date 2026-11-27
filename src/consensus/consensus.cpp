#include "phantomx/consensus/consensus.hpp"

#include <algorithm>
#include <unordered_map>

#include "phantomx/primitives/hash.hpp"

namespace phantomx::consensus {
namespace {
std::string op_key(const std::array<std::uint8_t, 32>& pk) {
  return to_hex(std::span<const std::uint8_t>(pk.data(), pk.size()));
}
}  // namespace

bool is_validator_eligible(const ValidatorEligibilityInput& in, const VotingPowerPolicy& policy) {
  if (in.slashed) return false;
  if (!in.identity_verified) return false;
  if (in.poi_score < policy.min_poi_score) return false;
  return true;
}

std::uint64_t compute_voting_power(const ValidatorEligibilityInput& in, const VotingPowerPolicy& policy) {
  if (!is_validator_eligible(in, policy)) return 0;
  // PoI-based eligibility/contribution weight.
  // Integer-only: base power from PoI score (0..100000) scaled down.
  std::uint64_t power = static_cast<std::uint64_t>(in.poi_score);
  if (policy.max_validator_voting_power != 0) {
    power = std::min(power, policy.max_validator_voting_power);
  }
  return power;
}

Result<ValidatorSet> build_validator_set(std::uint64_t epoch, const std::vector<ValidatorEligibilityInput>& inputs,
                                         const VotingPowerPolicy& policy) {
  ValidatorSet vs;
  vs.epoch = epoch;
  std::unordered_map<std::string, std::uint64_t> operator_power;
  for (const auto& in : inputs) {
    if (!is_validator_eligible(in, policy)) continue;
    std::uint64_t power = compute_voting_power(in, policy);
    if (power == 0) continue;
    const auto ok = op_key(in.operator_pubkey);
    if (policy.max_operator_voting_power != 0) {
      auto& used = operator_power[ok];
      const std::uint64_t remaining = (used >= policy.max_operator_voting_power) ? 0 : (policy.max_operator_voting_power - used);
      power = std::min(power, remaining);
      used += power;
    }
    if (power == 0) continue;
    Validator v;
    v.validator_pubkey = in.validator_pubkey;
    v.operator_pubkey = in.operator_pubkey;
    v.voting_power = power;
    v.active = true;
    vs.validators.push_back(v);
    vs.total_voting_power += power;
  }
  if (vs.validators.empty()) return MakeError(ErrorCode::kInvalidArgument, "no eligible validators");
  return vs;
}

}  // namespace phantomx::consensus
