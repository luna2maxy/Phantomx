#pragma once

#include <cstdint>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/poi/types.hpp"

namespace phantomx::poi {

struct BandwidthSample {
  std::array<std::uint8_t, 32> verifier_pubkey{};
  std::uint64_t upload_kbps = 0;
  std::uint64_t download_kbps = 0;
  std::uint64_t timestamp = 0;
  std::array<std::uint8_t, 32> challenge_nonce{};
  bool valid = false;
};

struct UptimeSample {
  std::uint64_t window_start = 0;
  std::uint64_t window_end = 0;
  std::uint64_t heartbeats_expected = 0;
  std::uint64_t heartbeats_ok = 0;
  std::uint64_t missed_heartbeats = 0;
};

struct LatencyProbe {
  std::string region;
  std::uint64_t rtt_ms = 0;
  std::uint64_t timestamp = 0;
  std::array<std::uint8_t, 32> verifier_pubkey{};
  bool valid = false;
};

struct RelayReceipt {
  std::array<std::uint8_t, 32> message_hash{};
  std::array<std::uint8_t, 32> sender_pubkey{};
  std::array<std::uint8_t, 32> receiver_pubkey{};
  std::array<std::uint8_t, 32> relay_pubkey{};
  std::uint64_t bytes = 0;
  std::uint64_t timestamp = 0;
  bool delivery_ack = false;
};

struct RelaySummary {
  std::uint64_t deliveries_ok = 0;
  std::uint64_t deliveries_expected = 0;
  std::uint64_t bytes_relayed = 0;
};

// Aggregation
[[nodiscard]] std::uint64_t median_u64(std::vector<std::uint64_t> values);
[[nodiscard]] std::uint64_t trimmed_mean_u64(std::vector<std::uint64_t> values, std::size_t trim_each_side);
[[nodiscard]] Result<BandwidthSample> aggregate_bandwidth(const std::vector<BandwidthSample>& samples);
[[nodiscard]] Result<std::uint64_t> aggregate_latency_rtt_ms(const std::vector<LatencyProbe>& probes);
[[nodiscard]] RelaySummary summarize_relay(const std::vector<RelayReceipt>& receipts);

}  // namespace phantomx::poi
