#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "phantomx/poi/evidence/evidence.hpp"
#include "phantomx/poi/measurement/measurement.hpp"
#include "phantomx/poi/types.hpp"

namespace phantomx::poi {

// TEST-ONLY simulator. Results MUST NOT be treated as mainnet proof.
struct SimulatorConfig {
  std::uint64_t upload_kbps = 50000;
  std::uint64_t download_kbps = 100000;
  std::uint64_t heartbeats_ok = 95;
  std::uint64_t heartbeats_expected = 100;
  std::uint64_t median_rtt_ms = 40;
  std::uint64_t relay_deliveries_ok = 90;
  std::uint64_t relay_deliveries_expected = 100;
  std::uint64_t relay_bytes = 1'000'000;
};

[[nodiscard]] PoiMetrics simulate_metrics(const SimulatorConfig& cfg);
[[nodiscard]] EvidenceRecord simulate_evidence(EvidenceKind kind, const std::array<std::uint8_t, 32>& subject_pubkey,
                                               const SimulatorConfig& cfg, std::uint64_t now);

}  // namespace phantomx::poi
