#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/verifier/registry.hpp"

namespace phantomx::verifier {

struct AssignmentRequest {
  VerifierType type = VerifierType::kPoi;
  std::array<std::uint8_t, 32> subject_pubkey{};
  std::array<std::uint8_t, 32> subject_operator_pubkey{};
  std::array<std::uint8_t, 32> pool_operator_pubkey{}; // optional, zero if none
  std::uint64_t epoch = 0;
  std::array<std::uint8_t, 32> randomness_seed{};
  std::size_t required_verifiers = 3;
};

struct AssignmentRecord {
  std::vector<std::array<std::uint8_t, 32>> verifiers;
  std::array<std::uint8_t, 32> assignment_hash{};
  bool auditable = true;
};

struct ConflictCheck {
  bool ok = true;
  std::string reason;
};

[[nodiscard]] ConflictCheck check_conflict(const VerifierRecord& verifier, const AssignmentRequest& req);
[[nodiscard]] Result<AssignmentRecord> assign_verifiers(const VerifierRegistry& registry, const AssignmentRequest& req);

}  // namespace phantomx::verifier
