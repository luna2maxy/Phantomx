#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/common/types.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::rewards {

inline constexpr std::uint64_t kPoiShareBps = 70000;      // 70%
inline constexpr std::uint64_t kVerifierShareBps = 20000;  // 20%
inline constexpr std::uint64_t kPoolShareBps = 10000;      // 10%
inline constexpr std::uint64_t kBpsDenom = 100000;

struct EpochParticipant {
  std::array<std::uint8_t, 32> id{}; // node/verifier/pool identity pubkey
  std::uint64_t work_units = 0;      // fixed-point or integer units, deterministic
};

struct EpochSnapshot {
  std::uint64_t epoch = 0;
  std::uint64_t start_height = 0;
  std::uint64_t end_height = 0;
  std::vector<EpochParticipant> poi_contributors;
  std::vector<EpochParticipant> verifier_operators;
  std::vector<EpochParticipant> pool_operators;
  std::uint64_t total_subsidy_atoms = 0;
};

struct RewardEntry {
  std::array<std::uint8_t, 32> recipient_id{};
  std::uint64_t amount_atoms = 0;
  std::string category; // "poi", "verifier", "pool"
};

struct RewardCommitment {
  std::uint64_t epoch = 0;
  std::uint64_t total_reward_atoms = 0;
  Hash256Bytes merkle_root{};
  std::vector<RewardEntry> entries; // canonical sorted; production may store only root + proofs
};

struct ClaimProofStep {
  Hash256Bytes hash{};
  bool is_left = false;
};

struct RewardClaim {
  std::uint64_t epoch = 0;
  std::array<std::uint8_t, 32> recipient_id{};
  std::uint64_t amount_atoms = 0;
  std::string category;
  std::vector<ClaimProofStep> merkle_proof;
};

[[nodiscard]] std::uint64_t block_subsidy_atoms(std::uint64_t height);
[[nodiscard]] std::uint64_t total_minted_atoms_through_height(std::uint64_t height);

[[nodiscard]] std::vector<RewardEntry> calculate_epoch_rewards(const EpochSnapshot& snapshot);
[[nodiscard]] RewardCommitment build_reward_commitment(const EpochSnapshot& snapshot);

[[nodiscard]] Hash256Bytes reward_leaf_hash(const RewardEntry& e);
[[nodiscard]] std::vector<ClaimProofStep> build_reward_merkle_proof(const std::vector<Hash256Bytes>& leaves, std::size_t index);
[[nodiscard]] bool verify_reward_merkle_proof(const Hash256Bytes& leaf, const std::vector<ClaimProofStep>& proof, const Hash256Bytes& root);

[[nodiscard]] bool verify_claim(const RewardCommitment& commitment, const RewardClaim& claim);

class ClaimRegistry {
 public:
  [[nodiscard]] bool has_claimed(std::uint64_t epoch, const std::array<std::uint8_t, 32>& recipient_id, const std::string& category) const;
  [[nodiscard]] Result<void> mark_claimed(std::uint64_t epoch, const std::array<std::uint8_t, 32>& recipient_id, const std::string& category);
  [[nodiscard]] std::uint64_t claimed_count() const noexcept { return claimed_.size(); }

 private:
  static std::string key(std::uint64_t epoch, const std::array<std::uint8_t, 32>& recipient_id, const std::string& category);
  std::unordered_set<std::string> claimed_;
};

}  // namespace phantomx::rewards
