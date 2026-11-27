#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "phantomx/primitives/hash.hpp"

namespace phantomx::poi {

// Fixed-point score scale: 100000 = 100.000%
using ScoreMilli = std::uint32_t;
inline constexpr ScoreMilli kScoreScale = 100000;
inline constexpr ScoreMilli kWeightBandwidth = 30000; // 30%
inline constexpr ScoreMilli kWeightUptime    = 25000; // 25%
inline constexpr ScoreMilli kWeightLatency   = 20000; // 20%
inline constexpr ScoreMilli kWeightRelay     = 25000; // 25%

enum class EvidenceKind : std::uint8_t {
  kBandwidth = 1,
  kUptime = 2,
  kLatency = 3,
  kRelay = 4,
};

struct VerifierAttestation {
  std::array<std::uint8_t, 32> verifier_pubkey{};
  std::array<std::uint8_t, 64> signature{};
  std::uint64_t timestamp = 0;
  Hash256Bytes evidence_hash{};
};

struct PoiMetrics {
  ScoreMilli bandwidth = 0;
  ScoreMilli uptime = 0;
  ScoreMilli latency = 0;
  ScoreMilli relay = 0;
};

struct PoiScore {
  ScoreMilli bandwidth = 0;
  ScoreMilli uptime = 0;
  ScoreMilli latency = 0;
  ScoreMilli relay = 0;
  ScoreMilli total = 0;
};

}  // namespace phantomx::poi
