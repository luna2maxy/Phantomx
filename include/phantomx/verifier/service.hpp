#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "phantomx/common/crypto.hpp"
#include "phantomx/common/error.hpp"
#include "phantomx/poi/evidence/evidence.hpp"
#include "phantomx/verifier/assignment.hpp"
#include "phantomx/verifier/registry.hpp"

namespace phantomx::verifier {

enum class DisputeStatus : std::uint8_t { kOpen = 1, kAccepted = 2, kRejected = 3 };

struct ChallengeTask {
  std::array<std::uint8_t, 32> challenge_id{};
  VerifierType type = VerifierType::kPoi;
  std::array<std::uint8_t, 32> subject_pubkey{};
  std::array<std::uint8_t, 32> verifier_pubkey{};
  std::uint64_t created_at = 0;
  std::uint64_t expires_at = 0;
  std::vector<std::uint8_t> payload;
  bool delivered = false;
};

struct SignedVerifierResult {
  std::array<std::uint8_t, 32> challenge_id{};
  std::array<std::uint8_t, 32> verifier_pubkey{};
  VerifierType type = VerifierType::kPoi;
  bool accepted = false;
  std::uint64_t timestamp = 0;
  Hash256Bytes evidence_hash{};
  std::array<std::uint8_t, 64> signature{};
};

struct DisputeRecord {
  std::array<std::uint8_t, 32> dispute_id{};
  std::array<std::uint8_t, 32> challenge_id{};
  std::array<std::uint8_t, 32> reporter_pubkey{};
  std::string reason;
  DisputeStatus status = DisputeStatus::kOpen;
  std::uint64_t created_at = 0;
};

[[nodiscard]] std::vector<std::uint8_t> serialize_result(const SignedVerifierResult& r);
[[nodiscard]] Hash256Bytes result_hash(const SignedVerifierResult& r);
[[nodiscard]] Result<SignedVerifierResult> sign_result(SignedVerifierResult r, const crypto::SecretKey& verifier_secret);
[[nodiscard]] bool verify_result_signature(const SignedVerifierResult& r);

class VerifierService {
 public:
  [[nodiscard]] Result<ChallengeTask> create_challenge(VerifierType type, const std::array<std::uint8_t, 32>& subject,
                                                       const std::array<std::uint8_t, 32>& verifier,
                                                       std::uint64_t now, std::uint32_t ttl_seconds,
                                                       std::vector<std::uint8_t> payload);
  [[nodiscard]] Result<void> deliver_challenge(ChallengeTask& task);
  [[nodiscard]] Result<SignedVerifierResult> verify_and_sign(const ChallengeTask& task, const poi::EvidenceRecord& evidence,
                                                            bool accepted, const crypto::SecretKey& verifier_secret,
                                                            const crypto::PublicKey& verifier_pub, std::uint64_t now);
  [[nodiscard]] Result<DisputeRecord> open_dispute(const SignedVerifierResult& result, const std::array<std::uint8_t, 32>& reporter,
                                                    std::string reason, std::uint64_t now);
  [[nodiscard]] Result<void> resolve_dispute(const std::array<std::uint8_t, 32>& dispute_id, DisputeStatus status);

  [[nodiscard]] const std::vector<DisputeRecord>& disputes() const noexcept { return disputes_; }

 private:
  std::vector<DisputeRecord> disputes_;
};

}  // namespace phantomx::verifier
