#pragma once

#include <cstdint>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/consensus/types.hpp"
#include "phantomx/poi/types.hpp"

namespace phantomx::consensus {

struct ValidatorEligibilityInput {
  std::array<std::uint8_t, 32> validator_pubkey{};
  std::array<std::uint8_t, 32> operator_pubkey{};
  poi::ScoreMilli poi_score = 0;
  std::uint64_t stake_or_bond_atoms = 0; // policy-defined
  bool identity_verified = false;
  bool slashed = false;
};

struct VotingPowerPolicy {
  poi::ScoreMilli min_poi_score = 50000; // 50.000%
  std::uint64_t max_operator_voting_power = 0; // 0 = disabled; policy cap
  std::uint64_t max_validator_voting_power = 0;
};

[[nodiscard]] bool is_validator_eligible(const ValidatorEligibilityInput& in, const VotingPowerPolicy& policy);
[[nodiscard]] std::uint64_t compute_voting_power(const ValidatorEligibilityInput& in, const VotingPowerPolicy& policy);
[[nodiscard]] Result<ValidatorSet> build_validator_set(std::uint64_t epoch, const std::vector<ValidatorEligibilityInput>& inputs,
                                                       const VotingPowerPolicy& policy);

}  // namespace phantomx::consensus
