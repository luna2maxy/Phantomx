#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include "phantomx/common/crypto.hpp"
#include "phantomx/common/error.hpp"
#include "phantomx/common/types.hpp"

namespace phantomx::poi {

inline constexpr std::uint8_t kPoiUptimeProofVersion = 1;
inline constexpr std::uint8_t kPoiChallengeTypeUptime = 1;
inline constexpr std::uint64_t kPoiUptimeMaxSkewMs = 300000;  // 5 min, explicit

struct PoiChallengeV1 {
  std::uint8_t version = kPoiUptimeProofVersion;
  std::uint64_t epoch = 0;
  std::uint64_t slot = 0;
  std::array<std::uint8_t, 32> challenge_id{};
  std::array<std::uint8_t, 32> verifier_pubkey{};
  std::array<std::uint8_t, 32> machine_id{};
  std::string owner_address;
  std::array<std::uint8_t, 32> nonce{};
  std::uint64_t issued_at_ms = 0;
  std::uint64_t expires_at_ms = 0;
  std::uint8_t challenge_type = kPoiChallengeTypeUptime;
  std::array<std::uint8_t, 32> params_hash{};
  std::array<std::uint8_t, 64> signature{};
};

struct PoiResponseV1 {
  std::uint8_t version = kPoiUptimeProofVersion;
  std::array<std::uint8_t, 32> challenge_id{};
  std::uint64_t epoch = 0;
  std::uint64_t slot = 0;
  std::array<std::uint8_t, 32> machine_id{};
  std::array<std::uint8_t, 32> machine_pubkey{};
  std::array<std::uint8_t, 32> response_hash{};
  std::uint64_t responded_at_ms = 0;
  std::array<std::uint8_t, 64> signature{};
};

struct PoiAttestationV1 {
  std::uint8_t version = kPoiUptimeProofVersion;
  std::array<std::uint8_t, 32> challenge_id{};
  std::uint64_t epoch = 0;
  std::uint64_t slot = 0;
  std::array<std::uint8_t, 32> machine_id{};
  std::array<std::uint8_t, 32> verifier_pubkey{};
  bool pass = false;
  std::uint32_t score_uptime_milli = 0; // 0..100000
  std::uint64_t observed_at_ms = 0;
  std::array<std::uint8_t, 32> evidence_hash{};
  std::array<std::uint8_t, 64> signature{};
};

struct PoiUptimeQuorumResult {
  bool quorum_met = false;
  std::uint32_t quorum_required = 0;
  std::uint32_t pass_count = 0;
  std::uint32_t fail_count = 0;
  std::uint32_t duplicate_count = 0;
  std::uint32_t invalid_count = 0;
  std::uint32_t conflict_count = 0;
  std::uint32_t score_uptime_milli = 0; // deterministic median of passing attestations
};

[[nodiscard]] std::array<std::uint8_t, 32> poi_challenge_signing_hash(const PoiChallengeV1& c);
[[nodiscard]] std::array<std::uint8_t, 32> poi_response_signing_hash(const PoiResponseV1& r);
[[nodiscard]] std::array<std::uint8_t, 32> poi_attestation_signing_hash(const PoiAttestationV1& a);

[[nodiscard]] Result<void> sign_poi_challenge(PoiChallengeV1& c, const crypto::SecretKey& sk);
[[nodiscard]] Result<void> sign_poi_response(PoiResponseV1& r, const crypto::SecretKey& sk);
[[nodiscard]] Result<void> sign_poi_attestation(PoiAttestationV1& a, const crypto::SecretKey& sk);

[[nodiscard]] Result<void> verify_poi_challenge(const PoiChallengeV1& c, std::uint64_t now_ms);
[[nodiscard]] Result<void> verify_poi_response(const PoiResponseV1& r, const PoiChallengeV1& c, std::uint64_t now_ms);
[[nodiscard]] Result<void> verify_poi_attestation(const PoiAttestationV1& a, const PoiChallengeV1& c, std::uint64_t now_ms);

class PoiUptimeReplayCache {
 public:
  // Returns true if newly inserted, false if replay/duplicate.
  bool mark_challenge(const std::array<std::uint8_t, 32>& challenge_id);
  bool mark_response(const std::array<std::uint8_t, 32>& challenge_id, const std::array<std::uint8_t, 32>& machine_pubkey);
  bool mark_attestation(const std::array<std::uint8_t, 32>& challenge_id, const std::array<std::uint8_t, 32>& verifier_pubkey);
  [[nodiscard]] std::size_t size() const;

 private:
  std::unordered_set<std::string> seen_;
};

[[nodiscard]] PoiUptimeQuorumResult aggregate_poi_uptime_attestations(
    const PoiChallengeV1& challenge,
    const std::vector<PoiAttestationV1>& attestations,
    const std::unordered_set<std::string>& active_verifier_pubkeys_hex,
    std::uint32_t quorum_required,
    std::uint64_t now_ms);

}  // namespace phantomx::poi
