#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "phantomx/primitives/hash.hpp"

namespace phantomx::consensus {

inline constexpr std::uint32_t kBftQuorumNumerator = 2;
inline constexpr std::uint32_t kBftQuorumDenominator = 3; // > 2/3

struct Validator {
  std::array<std::uint8_t, 32> validator_pubkey{};
  std::array<std::uint8_t, 32> operator_pubkey{};
  std::uint64_t voting_power = 0; // integer, policy-assigned
  bool active = true;
};

struct ValidatorSet {
  std::uint64_t epoch = 0;
  std::vector<Validator> validators;
  std::uint64_t total_voting_power = 0;
};

enum class VotePhase : std::uint8_t { kPrevote = 1, kPrecommit = 2 };

struct Vote {
  VotePhase phase = VotePhase::kPrevote;
  std::uint64_t epoch = 0;
  std::uint64_t view = 0;
  std::uint32_t height = 0;
  Hash256Bytes block_hash{};
  std::array<std::uint8_t, 32> validator_pubkey{};
  std::uint64_t voting_power = 0;
  std::array<std::uint8_t, 64> signature{};
};

struct QuorumCertificate {
  VotePhase phase = VotePhase::kPrecommit;
  std::uint64_t epoch = 0;
  std::uint64_t view = 0;
  std::uint32_t height = 0;
  Hash256Bytes block_hash{};
  std::vector<Vote> votes;
  std::uint64_t signed_voting_power = 0;
};

struct BlockProposal {
  std::uint64_t epoch = 0;
  std::uint64_t view = 0;
  std::uint32_t height = 0;
  Hash256Bytes block_hash{};
  Hash256Bytes parent_hash{};
  std::array<std::uint8_t, 32> proposer_pubkey{};
  std::uint64_t timestamp = 0;
  std::optional<QuorumCertificate> justify_qc;
  std::array<std::uint8_t, 64> signature{};
};

struct FinalityCertificate {
  std::uint32_t height = 0;
  Hash256Bytes block_hash{};
  QuorumCertificate precommit_qc;
};

struct ViewChange {
  std::uint64_t epoch = 0;
  std::uint32_t height = 0;
  std::uint64_t new_view = 0;
  std::array<std::uint8_t, 32> validator_pubkey{};
  std::uint64_t voting_power = 0;
  std::optional<QuorumCertificate> high_qc;
  std::array<std::uint8_t, 64> signature{};
};

struct ViewChangeQC {
  std::uint64_t epoch = 0;
  std::uint32_t height = 0;
  std::uint64_t new_view = 0;
  std::vector<ViewChange> votes;
  std::uint64_t signed_voting_power = 0;
};

struct NewView {
  std::uint64_t epoch = 0;
  std::uint32_t height = 0;
  std::uint64_t view = 0;
  ViewChangeQC view_change_qc;
  std::optional<BlockProposal> proposal;
  std::array<std::uint8_t, 32> proposer_pubkey{};
  std::array<std::uint8_t, 64> signature{};
};

}  // namespace phantomx::consensus
