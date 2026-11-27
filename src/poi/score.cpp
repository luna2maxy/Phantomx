#include "phantomx/poi/score.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace phantomx::poi {
namespace {
ScoreMilli ratio_score(std::uint64_t num, std::uint64_t den) {
  if (den == 0) return 0;
  std::uint64_t v = (num * static_cast<std::uint64_t>(kScoreScale)) / den;
  if (v > kScoreScale) v = kScoreScale;
  return static_cast<ScoreMilli>(v);
}
}  // namespace

ScoreMilli clamp_score(ScoreMilli value) { return std::min<ScoreMilli>(value, kScoreScale); }

PoiScore compute_poi_score(const PoiMetrics& metrics) {
  PoiScore s;
  s.bandwidth = clamp_score(metrics.bandwidth);
  s.uptime = clamp_score(metrics.uptime);
  s.latency = clamp_score(metrics.latency);
  s.relay = clamp_score(metrics.relay);
  // Weighted sum in fixed-point: sum(component * weight) / 100000
  const std::uint64_t total =
      (static_cast<std::uint64_t>(s.bandwidth) * kWeightBandwidth +
       static_cast<std::uint64_t>(s.uptime) * kWeightUptime +
       static_cast<std::uint64_t>(s.latency) * kWeightLatency +
       static_cast<std::uint64_t>(s.relay) * kWeightRelay) /
      static_cast<std::uint64_t>(kScoreScale);
  s.total = clamp_score(static_cast<ScoreMilli>(total));
  return s;
}

ScoreMilli score_bandwidth(std::uint64_t upload_kbps, std::uint64_t download_kbps,
                           std::uint64_t target_upload_kbps, std::uint64_t target_download_kbps) {
  const auto up = ratio_score(upload_kbps, target_upload_kbps);
  const auto down = ratio_score(download_kbps, target_download_kbps);
  // 50/50 upload/download within bandwidth component
  return clamp_score(static_cast<ScoreMilli>((static_cast<std::uint64_t>(up) + down) / 2));
}

ScoreMilli score_uptime(std::uint64_t heartbeats_ok, std::uint64_t heartbeats_expected) {
  return ratio_score(heartbeats_ok, heartbeats_expected);
}

ScoreMilli score_latency(std::uint64_t median_rtt_ms, std::uint64_t target_rtt_ms, std::uint64_t max_rtt_ms) {
  if (median_rtt_ms <= target_rtt_ms) return kScoreScale;
  if (median_rtt_ms >= max_rtt_ms || max_rtt_ms <= target_rtt_ms) return 0;
  // Linear decay between target and max.
  const std::uint64_t span = max_rtt_ms - target_rtt_ms;
  const std::uint64_t over = median_rtt_ms - target_rtt_ms;
  const std::uint64_t remaining = kScoreScale - (over * kScoreScale) / span;
  return clamp_score(static_cast<ScoreMilli>(remaining));
}

ScoreMilli score_relay(std::uint64_t deliveries_ok, std::uint64_t deliveries_expected,
                       std::uint64_t bytes_relayed, std::uint64_t target_bytes) {
  const auto delivery = ratio_score(deliveries_ok, deliveries_expected);
  const auto bytes = ratio_score(bytes_relayed, target_bytes);
  // 70% delivery success, 30% accounted bytes
  const std::uint64_t v = (static_cast<std::uint64_t>(delivery) * 70 + static_cast<std::uint64_t>(bytes) * 30) / 100;
  return clamp_score(static_cast<ScoreMilli>(v));
}

std::uint64_t poi_epoch_level(std::uint64_t active_miners, std::uint64_t miners_per_epoch) {
  if (miners_per_epoch == 0) miners_per_epoch = 1;
  const auto e = (active_miners + miners_per_epoch - 1) / miners_per_epoch; // ceil
  return e < 1 ? 1 : e;
}

std::uint64_t poi_target_block_interval_ms(std::uint64_t base_ms, double avg_score, std::uint64_t epoch,
                                           std::uint64_t min_ms, std::uint64_t max_ms) {
  const double f_score = std::clamp(avg_score / 50.0, 0.5, 1.5);
  const double f_epoch = std::clamp(static_cast<double>(epoch) / 1.0, 0.75, 1.5);
  const double t = static_cast<double>(base_ms) / (f_score * f_epoch);
  return static_cast<std::uint64_t>(std::clamp(t, static_cast<double>(min_ms), static_cast<double>(max_ms)));
}

namespace {
bool finite_nonneg(double v) { return std::isfinite(v) && v >= 0.0; }
double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
}  // namespace

double poi_score_bandwidth_mbps(double bandwidth_mbps, double bw_ref_mbps) {
  if (!finite_nonneg(bandwidth_mbps) || !std::isfinite(bw_ref_mbps) || bw_ref_mbps <= 0.0) return -1.0;
  if (bandwidth_mbps == 0.0) return 0.0;
  // Diminishing returns, no hard cap: S_bw = B / (B + K)
  const double denom = bandwidth_mbps + bw_ref_mbps;
  if (!std::isfinite(denom) || denom <= 0.0) return -1.0;
  return clamp01(bandwidth_mbps / denom);
}

double poi_score_latency_ms(double latency_ms, double lat_ref_ms) {
  if (!finite_nonneg(latency_ms) || !std::isfinite(lat_ref_ms) || lat_ref_ms <= 0.0) return -1.0;
  // Continuous diminishing returns: S_lat = 1 / (1 + L/ref)
  const double denom = 1.0 + (latency_ms / lat_ref_ms);
  if (!std::isfinite(denom) || denom <= 0.0) return -1.0;
  return clamp01(1.0 / denom);
}

double poi_score_uptime_ratio(double uptime_ratio) {
  if (!std::isfinite(uptime_ratio) || uptime_ratio < 0.0 || uptime_ratio > 1.0) return -1.0;
  return clamp01(uptime_ratio);
}

double poi_score_relay_ratio(double relay_ratio) {
  if (!std::isfinite(relay_ratio) || relay_ratio < 0.0 || relay_ratio > 1.0) return -1.0;
  return clamp01(relay_ratio);
}

double poi_score_network(std::uint64_t active_miners, double m_ref) {
  if (!std::isfinite(m_ref) || m_ref <= 0.0) return -1.0;
  return clamp01(static_cast<double>(active_miners) / m_ref);
}

PoiCompositeScore poi_composite_score_v2(const PoiCompositeInput& in) {
  PoiCompositeScore r;
  double weighted = 0.0;
  double weight_sum = 0.0;

  if (in.bandwidth_valid) {
    r.bandwidth = poi_score_bandwidth_mbps(static_cast<double>(in.bandwidth_mbps), in.bw_ref_mbps);
  }
  if (r.bandwidth >= 0.0) { weighted += kPoiWBandwidth * r.bandwidth; weight_sum += kPoiWBandwidth; }

  r.latency = poi_score_latency_ms(in.latency_ms, in.lat_ref_ms);
  if (r.latency >= 0.0) { weighted += kPoiWLatency * r.latency; weight_sum += kPoiWLatency; }

  r.uptime = poi_score_uptime_ratio(in.uptime_ratio);
  if (r.uptime >= 0.0) { weighted += kPoiWUptime * r.uptime; weight_sum += kPoiWUptime; }

  r.relay = poi_score_relay_ratio(in.relay_ratio);
  if (r.relay >= 0.0) { weighted += kPoiWRelay * r.relay; weight_sum += kPoiWRelay; }

  if (in.network_valid) {
    r.network = poi_score_network(in.active_miners, in.m_ref);
  }
  if (r.network >= 0.0) { weighted += kPoiWNetwork * r.network; weight_sum += kPoiWNetwork; }

  if (weight_sum <= 0.0) return r;
  r.available = true;
  r.partial = (r.bandwidth < 0.0 || r.latency < 0.0 || r.uptime < 0.0 || r.relay < 0.0 || r.network < 0.0);
  // Normalize by available weights so missing invalid components cannot get a perfect score.
  r.score = clamp01(weighted / weight_sum) * 100.0;
  return r;
}

PoiCompositeScore poi_composite_score(std::uint64_t bandwidth_bytes, double latency_ms, double relay_ratio,
                                      std::uint64_t active_miners, double bw_ref_mb, double lat_ref_ms,
                                      double m_ref) {
  PoiCompositeInput in;
  if (bandwidth_bytes > 0) {
    // Legacy arg name bw_ref_mb is treated as Mbps reference for the new formula.
    // bandwidth_bytes converted to Mbps (decimal megabits) for protocol scoring.
    const double mbps = (static_cast<double>(bandwidth_bytes) * 8.0) / 1000000.0;
    if (std::isfinite(mbps) && mbps >= 0.0 && mbps <= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
      in.bandwidth_mbps = static_cast<std::uint64_t>(mbps);
      in.bandwidth_valid = (mbps > 0.0);
    }
  }
  in.latency_ms = latency_ms;
  in.uptime_ratio = -1.0; // legacy wrapper has no uptime; missing uptime must not be perfect
  in.relay_ratio = relay_ratio;
  in.active_miners = active_miners;
  in.network_valid = true;
  in.bw_ref_mbps = (bw_ref_mb > 0.0 ? bw_ref_mb : kPoiBwRefMbps);
  in.lat_ref_ms = (lat_ref_ms > 0.0 ? lat_ref_ms : kPoiLatRefMs);
  in.m_ref = (m_ref > 0.0 ? m_ref : kPoiMRef);
  return poi_composite_score_v2(in);
}

std::string format_poi_bandwidth_si(double mbps) {
  if (!std::isfinite(mbps) || mbps < 0.0) return "Not Available";
  char buf[64];
  if (mbps < 1000.0) {
    std::snprintf(buf, sizeof(buf), "%.2f Mbps", mbps);
  } else if (mbps < 1000000.0) {
    std::snprintf(buf, sizeof(buf), "%.2f Gbps", mbps / 1000.0);
  } else if (mbps < 1000000000.0) {
    std::snprintf(buf, sizeof(buf), "%.2f Tbps", mbps / 1000000.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%.2f Pbps", mbps / 1000000000.0);
  }
  return std::string(buf);
}

}  // namespace phantomx::poi
