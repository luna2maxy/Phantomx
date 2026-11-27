#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "phantomx/common/crypto.hpp"
#include "phantomx/common/error.hpp"
#include "phantomx/poi/types.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::poi {

struct Challenge {
  EvidenceKind kind = EvidenceKind::kBandwidth;
  std::array<std::uint8_t, 32> nonce{};
  std::uint64_t issued_at = 0;    // unix seconds
  std::uint64_t expires_at = 0;   // unix seconds
  std::array<std::uint8_t, 32> verifier_pubkey{};
  std::array<std::uint8_t, 32> subject_pubkey{}; // node being measured
  std::uint32_t payload_size = 0; // for bandwidth
  std::uint32_t probe_count = 0;  // for latency/uptime
  std::string region;             // for latency
};

[[nodiscard]] std::vector<std::uint8_t> serialize_challenge(const Challenge& c);
[[nodiscard]] Hash256Bytes challenge_id(const Challenge& c);

class NonceRegistry {
 public:
  [[nodiscard]] bool mark_used(const std::array<std::uint8_t, 32>& nonce, std::uint64_t now, std::uint64_t ttl_seconds = 3600);
  void prune(std::uint64_t now, std::uint64_t ttl_seconds = 3600);
  [[nodiscard]] std::size_t size() const noexcept { return seen_.size(); }

 private:
  struct Entry { std::array<std::uint8_t, 32> nonce{}; std::uint64_t seen_at = 0; };
  std::vector<Entry> seen_;
};

[[nodiscard]] Result<Challenge> make_random_challenge(EvidenceKind kind, const crypto::PublicKey& verifier,
                                                     const crypto::PublicKey& subject, std::uint64_t now,
                                                     std::uint32_t ttl_seconds = 30);

}  // namespace phantomx::poi
