#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "phantomx/common/crypto.hpp"
#include "phantomx/common/error.hpp"
#include "phantomx/poi/types.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::poi {

struct EvidenceRecord {
  EvidenceKind kind = EvidenceKind::kBandwidth;
  std::array<std::uint8_t, 32> subject_pubkey{};
  std::array<std::uint8_t, 32> challenge_nonce{};
  std::uint64_t window_start = 0;
  std::uint64_t window_end = 0;
  std::uint64_t measured_value_a = 0; // e.g. upload_kbps / heartbeats_ok / rtt_ms / deliveries_ok
  std::uint64_t measured_value_b = 0; // e.g. download_kbps / expected / probe_count / expected
  std::uint64_t measured_value_c = 0; // e.g. bytes_relayed
  ScoreMilli component_score = 0;
  bool simulator = false; // MUST be false for mainnet-valid evidence
  std::vector<VerifierAttestation> attestations;
};

[[nodiscard]] std::vector<std::uint8_t> serialize_evidence(const EvidenceRecord& e);
[[nodiscard]] Hash256Bytes evidence_hash(const EvidenceRecord& e);

[[nodiscard]] Result<VerifierAttestation> sign_evidence(const EvidenceRecord& e, const crypto::SecretKey& verifier_secret,
                                                        const crypto::PublicKey& verifier_pub, std::uint64_t timestamp);
[[nodiscard]] bool verify_attestation(const EvidenceRecord& e, const VerifierAttestation& a);
[[nodiscard]] bool evidence_has_independent_attestation(const EvidenceRecord& e, std::size_t min_attestations = 1);

}  // namespace phantomx::poi
