#pragma once

#include <cstdint>

#include "phantomx/poi/types.hpp"

namespace phantomx::poi {

// All inputs/outputs are fixed-point milli-percent (0..100000).
[[nodiscard]] ScoreMilli clamp_score(ScoreMilli value);
[[nodiscard]] PoiScore compute_poi_score(const PoiMetrics& metrics);

// Component calculators (fixed-point). Inputs are raw measurements; outputs clamped 0..100000.
[[nodiscard]] ScoreMilli score_bandwidth(std::uint64_t upload_kbps, std::uint64_t download_kbps,
                                        std::uint64_t target_upload_kbps, std::uint64_t target_download_kbps);
[[nodiscard]] ScoreMilli score_uptime(std::uint64_t heartbeats_ok, std::uint64_t heartbeats_expected);
[[nodiscard]] ScoreMilli score_latency(std::uint64_t median_rtt_ms, std::uint64_t target_rtt_ms, std::uint64_t max_rtt_ms);
[[nodiscard]] ScoreMilli score_relay(std::uint64_t deliveries_ok, std::uint64_t deliveries_expected,
                                     std::uint64_t bytes_relayed, std::uint64_t target_bytes);

// --- PoI composite scoring (deterministic formula v2) ---
// All component scores are normalized to [0,1]. Invalid/missing inputs are -1.
// Composite: 0.30*S_bw + 0.20*S_lat + 0.25*S_uptime + 0.15*S_relay + 0.10*S_network
struct PoiCompositeScore {
  bool available = false;
  bool partial = false;
  double score = 0.0;  // 0..100
  double bandwidth = -1.0;  // [0,1] or -1 unavailable/invalid
  double latency = -1.0;    // [0,1] or -1 unavailable/invalid
  double uptime = -1.0;     // [0,1] or -1 unavailable/invalid
  double relay = -1.0;      // [0,1] or -1 unavailable/invalid
  double network = -1.0;    // [0,1] or -1 unavailable/invalid
};

struct PoiCompositeInput {
  std::uint64_t bandwidth_mbps = 0;      // verified bandwidth in Mbps; 0 means unavailable
  bool bandwidth_valid = false;
  double latency_ms = -1.0;              // <0 or non-finite means unavailable/invalid
  double uptime_ratio = -1.0;            // [0,1], <0 or non-finite means unavailable/invalid
  double relay_ratio = -1.0;             // [0,1], <0 or non-finite means unavailable/invalid
  std::uint64_t active_miners = 0;
  bool network_valid = false;
  double bw_ref_mbps = 500.0;            // protocol param: bandwidth midpoint
  double lat_ref_ms = 100.0;             // protocol param: latency reference
  double m_ref = 50.0;                   // protocol param: network reference miners
};

inline constexpr double kPoiWBandwidth = 0.30;
inline constexpr double kPoiWLatency = 0.20;
inline constexpr double kPoiWUptime = 0.25;
inline constexpr double kPoiWRelay = 0.15;
inline constexpr double kPoiWNetwork = 0.10;
inline constexpr double kPoiBwRefMbps = 500.0;
inline constexpr double kPoiLatRefMs = 100.0;
inline constexpr double kPoiMRef = 50.0;
inline constexpr std::uint64_t kPoiUptimeWindowSec = 24ULL * 60ULL * 60ULL;

[[nodiscard]] std::uint64_t poi_epoch_level(std::uint64_t active_miners, std::uint64_t miners_per_epoch = 10);
[[nodiscard]] std::uint64_t poi_target_block_interval_ms(std::uint64_t base_ms, double avg_score, std::uint64_t epoch,
                                                         std::uint64_t min_ms = 15000, std::uint64_t max_ms = 300000);

// Normalized component scorers. Return [0,1], or -1.0 for invalid/unavailable input.
[[nodiscard]] double poi_score_bandwidth_mbps(double bandwidth_mbps, double bw_ref_mbps = kPoiBwRefMbps);
[[nodiscard]] double poi_score_latency_ms(double latency_ms, double lat_ref_ms = kPoiLatRefMs);
[[nodiscard]] double poi_score_uptime_ratio(double uptime_ratio);
[[nodiscard]] double poi_score_relay_ratio(double relay_ratio);
[[nodiscard]] double poi_score_network(std::uint64_t active_miners, double m_ref = kPoiMRef);

// Primary composite scorer. Invalid/missing components are excluded from weight sum;
// missing uptime does NOT get a perfect score.
[[nodiscard]] PoiCompositeScore poi_composite_score_v2(const PoiCompositeInput& in);

// Backward-compatible wrapper (legacy args). bandwidth_bytes is interpreted as bytes and
// converted to Mbps using decimal megabits (bytes*8/1e6) for scoring.
[[nodiscard]] PoiCompositeScore poi_composite_score(std::uint64_t bandwidth_bytes, double latency_ms, double relay_ratio,
                                                    std::uint64_t active_miners, double bw_ref_mb = kPoiBwRefMbps,
                                                    double lat_ref_ms = kPoiLatRefMs, double m_ref = kPoiMRef);

// SI bits/sec display for PoI bandwidth (natural for PoI; not PoW hashrate).
// 1000-based: Mbps, Gbps, Tbps, Pbps.
[[nodiscard]] std::string format_poi_bandwidth_si(double mbps);

}  // namespace phantomx::poi
