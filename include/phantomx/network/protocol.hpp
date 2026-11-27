#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/consensus/types.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::network {

inline constexpr std::uint32_t kP2PProtocolVersion = 1;
inline constexpr std::uint32_t kMaxMessageSize = 2 * 1024 * 1024;  // 2 MiB
inline constexpr std::uint32_t kMaxInventory = 1024;
inline constexpr std::uint32_t kMaxAddr = 64;
inline constexpr std::size_t kMaxConnections = 64;
inline constexpr std::size_t kMaxInboundConnections = 48;
inline constexpr std::uint32_t kHandshakeTimeoutMs = 5000;
inline constexpr std::uint32_t kIdleTimeoutMs = 30000;

enum class MessageType : std::uint8_t {
  kHello = 1,
  kHelloAck = 2,
  kPing = 3,
  kPong = 4,
  kGetAddr = 5,
  kAddr = 6,
  kInv = 7,
  kGetData = 8,
  kBlock = 9,
  kTx = 10,
  kGetHeaders = 11,
  kHeaders = 12,
  kReject = 13,
  kPoiAttestation = 14,
  kPoiAttestationAck = 15,
  kBftVote = 16,
  kBftProposal = 17,
  kBftQc = 18,
  kBftFinality = 19,
  kBftBlock = 20,
  kBftViewChange = 21,
  kBftNewView = 22,
};

enum class InventoryType : std::uint8_t {
  kTx = 1,
  kBlock = 2,
};

struct InventoryItem {
  InventoryType type = InventoryType::kTx;
  Hash256Bytes hash{};
};

struct NetAddress {
  std::string host;
  std::uint16_t port = 0;
  std::uint64_t services = 0;
  std::int64_t last_seen = 0;
};

struct HelloMessage {
  std::uint32_t network_magic = 0;
  std::uint32_t protocol_version = kP2PProtocolVersion;
  std::uint64_t nonce = 0;
  std::uint64_t timestamp = 0;
  Hash256Bytes genesis_hash{};
  std::string agent;
  std::uint16_t listen_port = 0;
};

struct RejectMessage {
  std::uint8_t code = 0;
  std::string reason;
};

struct Message {
  std::uint32_t network_magic = 0;
  MessageType type = MessageType::kPing;
  std::vector<std::uint8_t> payload;
};

[[nodiscard]] std::uint32_t payload_checksum(std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t> encode_message(const Message& msg);
[[nodiscard]] Result<Message> decode_message(std::span<const std::uint8_t> bytes, std::uint32_t expected_magic);

// Payload codecs
[[nodiscard]] std::vector<std::uint8_t> encode_hello(const HelloMessage& m);
[[nodiscard]] Result<HelloMessage> decode_hello(std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t> encode_pingpong(std::uint64_t nonce);
[[nodiscard]] Result<std::uint64_t> decode_pingpong(std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t> encode_inv(const std::vector<InventoryItem>& items);
[[nodiscard]] Result<std::vector<InventoryItem>> decode_inv(std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t> encode_addr(const std::vector<NetAddress>& addrs);
[[nodiscard]] Result<std::vector<NetAddress>> decode_addr(std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t> encode_hash(const Hash256Bytes& hash);
[[nodiscard]] Result<Hash256Bytes> decode_hash(std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t> encode_reject(const RejectMessage& r);
[[nodiscard]] Result<RejectMessage> decode_reject(std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t> encode_bft_vote(const consensus::Vote& vote);
[[nodiscard]] Result<consensus::Vote> decode_bft_vote(std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> encode_bft_proposal(const consensus::BlockProposal& proposal);
[[nodiscard]] Result<consensus::BlockProposal> decode_bft_proposal(std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> encode_bft_qc(const consensus::QuorumCertificate& qc);
[[nodiscard]] Result<consensus::QuorumCertificate> decode_bft_qc(std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> encode_bft_finality(const consensus::FinalityCertificate& cert);
[[nodiscard]] Result<consensus::FinalityCertificate> decode_bft_finality(std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> encode_bft_view_change(const consensus::ViewChange& vc);
[[nodiscard]] Result<consensus::ViewChange> decode_bft_view_change(std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> encode_bft_new_view(const consensus::NewView& nv);
[[nodiscard]] Result<consensus::NewView> decode_bft_new_view(std::span<const std::uint8_t> payload);

}  // namespace phantomx::network
