#include "phantomx/poi/measurement/measurement.hpp"

#include <algorithm>
#include <numeric>

namespace phantomx::poi {

std::uint64_t median_u64(std::vector<std::uint64_t> values) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const auto mid = values.size() / 2;
  if (values.size() % 2 == 1) return values[mid];
  return (values[mid - 1] / 2) + (values[mid] / 2) + ((values[mid - 1] % 2 + values[mid] % 2) / 2);
}

std::uint64_t trimmed_mean_u64(std::vector<std::uint64_t> values, std::size_t trim_each_side) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  if (values.size() <= trim_each_side * 2) return median_u64(values);
  std::size_t begin = trim_each_side;
  std::size_t end = values.size() - trim_each_side;
  std::uint64_t sum = 0;
  for (std::size_t i = begin; i < end; ++i) sum += values[i];
  return sum / (end - begin);
}

Result<BandwidthSample> aggregate_bandwidth(const std::vector<BandwidthSample>& samples) {
  std::vector<std::uint64_t> ups;
  std::vector<std::uint64_t> downs;
  for (const auto& s : samples) {
    if (!s.valid) continue;
    ups.push_back(s.upload_kbps);
    downs.push_back(s.download_kbps);
  }
  if (ups.empty()) return MakeError(ErrorCode::kNotFound, "no valid bandwidth samples");
  BandwidthSample out;
  out.valid = true;
  out.upload_kbps = median_u64(ups);
  out.download_kbps = median_u64(downs);
  return out;
}

Result<std::uint64_t> aggregate_latency_rtt_ms(const std::vector<LatencyProbe>& probes) {
  std::vector<std::uint64_t> rtts;
  for (const auto& p : probes) {
    if (!p.valid) continue;
    if (p.rtt_ms == 0) continue;
    rtts.push_back(p.rtt_ms);
  }
  if (rtts.empty()) return MakeError(ErrorCode::kNotFound, "no valid latency probes");
  // Robust aggregation: trimmed mean after removing one outlier on each side when possible.
  const std::size_t trim = rtts.size() >= 5 ? 1 : 0;
  return trimmed_mean_u64(std::move(rtts), trim);
}

RelaySummary summarize_relay(const std::vector<RelayReceipt>& receipts) {
  RelaySummary s;
  s.deliveries_expected = receipts.size();
  for (const auto& r : receipts) {
    if (r.delivery_ack) {
      ++s.deliveries_ok;
      s.bytes_relayed += r.bytes;
    }
  }
  return s;
}

}  // namespace phantomx::poi
