#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "phantomx/common/error.hpp"

namespace phantomx::verifier {

enum class VerifierType : std::uint8_t { kPoi = 1 };
enum class VerifierStatus : std::uint8_t { kActive = 1, kOffline = 2, kSuspended = 3, kBanned = 4 };

struct VerifierRecord {
  std::array<std::uint8_t, 32> verifier_pubkey{};
  std::array<std::uint8_t, 32> operator_pubkey{}; // separate operator identity
  VerifierType type = VerifierType::kPoi;
  VerifierStatus status = VerifierStatus::kActive;
  std::string endpoint;
  std::uint64_t registered_at = 0;
  std::uint64_t last_seen = 0;
  std::int64_t reputation = 0;
  std::uint64_t reward_atoms_accounted = 0;
  std::uint64_t jobs_completed = 0;
  std::uint64_t jobs_failed = 0;
};

struct VerifierEligibility {
  bool eligible = false;
  std::string reason;
};

class VerifierRegistry {
 public:
  [[nodiscard]] Result<void> register_verifier(VerifierRecord v, std::uint64_t now);
  [[nodiscard]] Result<void> mark_offline(const std::array<std::uint8_t, 32>& verifier_pubkey, std::uint64_t now);
  [[nodiscard]] Result<void> mark_active(const std::array<std::uint8_t, 32>& verifier_pubkey, std::uint64_t now);
  [[nodiscard]] Result<void> suspend(const std::array<std::uint8_t, 32>& verifier_pubkey, std::string reason);
  [[nodiscard]] Result<void> update_reputation(const std::array<std::uint8_t, 32>& verifier_pubkey, std::int64_t delta);
  [[nodiscard]] Result<void> account_reward(const std::array<std::uint8_t, 32>& verifier_pubkey, std::uint64_t atoms);

  [[nodiscard]] const VerifierRecord* find(const std::array<std::uint8_t, 32>& verifier_pubkey) const;
  [[nodiscard]] VerifierEligibility eligible_for(const std::array<std::uint8_t, 32>& verifier_pubkey, VerifierType required) const;
  [[nodiscard]] std::vector<VerifierRecord> active_verifiers(VerifierType required) const;
  [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }

 private:
  static std::string key(const std::array<std::uint8_t, 32>& pk);
  std::unordered_map<std::string, VerifierRecord> records_;
};

}  // namespace phantomx::verifier
