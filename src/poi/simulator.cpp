#include "phantomx/poi/simulator.hpp"

#include "phantomx/poi/score.hpp"

namespace phantomx::poi {

PoiMetrics simulate_metrics(const SimulatorConfig& cfg) {
  PoiMetrics m;
  m.bandwidth = score_bandwidth(cfg.upload_kbps, cfg.download_kbps, 100000, 200000);
  m.uptime = score_uptime(cfg.heartbeats_ok, cfg.heartbeats_expected);
  m.latency = score_latency(cfg.median_rtt_ms, 50, 300);
  m.relay = score_relay(cfg.relay_deliveries_ok, cfg.relay_deliveries_expected, cfg.relay_bytes, 2'000'000);
  return m;
}

EvidenceRecord simulate_evidence(EvidenceKind kind, const std::array<std::uint8_t, 32>& subject_pubkey,
                                 const SimulatorConfig& cfg, std::uint64_t now) {
  EvidenceRecord e;
  e.kind = kind;
  e.subject_pubkey = subject_pubkey;
  e.window_start = now > 3600 ? now - 3600 : 0;
  e.window_end = now;
  e.simulator = true; // never mainnet proof

  auto metrics = simulate_metrics(cfg);
  switch (kind) {
    case EvidenceKind::kBandwidth:
      e.measured_value_a = cfg.upload_kbps;
      e.measured_value_b = cfg.download_kbps;
      e.component_score = metrics.bandwidth;
      break;
    case EvidenceKind::kUptime:
      e.measured_value_a = cfg.heartbeats_ok;
      e.measured_value_b = cfg.heartbeats_expected;
      e.component_score = metrics.uptime;
      break;
    case EvidenceKind::kLatency:
      e.measured_value_a = cfg.median_rtt_ms;
      e.measured_value_b = 5;
      e.component_score = metrics.latency;
      break;
    case EvidenceKind::kRelay:
      e.measured_value_a = cfg.relay_deliveries_ok;
      e.measured_value_b = cfg.relay_deliveries_expected;
      e.measured_value_c = cfg.relay_bytes;
      e.component_score = metrics.relay;
      break;
  }
  return e;
}

}  // namespace phantomx::poi
