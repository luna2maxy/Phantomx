#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

#include "phantomx/network/protocol.hpp"

namespace phantomx::network {

enum class PeerState : std::uint8_t {
  kConnecting = 0,
  kHandshaking = 1,
  kReady = 2,
  kBanned = 3,
  kDisconnected = 4,
};

struct PeerScore {
  int score = 0;
  std::uint32_t invalid_messages = 0;
  std::uint32_t timeouts = 0;
  std::uint32_t spam_events = 0;
};

struct PeerInfo {
  std::uint64_t id = 0;
  std::string host;
  std::uint16_t port = 0;
  std::uint16_t listen_port = 0;
  // Local (source) TCP port for outbound connections. Together with the remote
  // port this forms the initiator's ephemeral port, which both endpoints of a
  // single TCP connection can derive consistently (useful for peer dedup).
  std::uint16_t local_port = 0;
  bool inbound = false;
  PeerState state = PeerState::kConnecting;
  PeerScore score;
  std::uint64_t nonce = 0;
  std::string agent;
  std::chrono::steady_clock::time_point connected_at{};
  std::chrono::steady_clock::time_point last_seen{};
  std::int64_t latency_ms = -1;  // -1 = not measured
};

inline constexpr int kBanScore = -100;
inline constexpr int kScoreInvalidMessage = -20;
inline constexpr int kScoreTimeout = -10;
inline constexpr int kScoreSpam = -15;
inline constexpr int kScoreGoodMessage = 1;

}  // namespace phantomx::network
