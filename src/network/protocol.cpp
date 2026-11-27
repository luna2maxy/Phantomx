#include "phantomx/network/protocol.hpp"

#include <cstring>

#include "phantomx/common/serialization.hpp"
#include "phantomx/consensus/bft.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::network {
namespace {

Result<std::string> read_lp_string(BinaryReader& r) {
  auto len = r.read_u32();
  if (!len.ok()) return len.error();
  if (len.value() > 1024) return MakeError(ErrorCode::kInvalidArgument, "string too large");
  auto bytes = r.read_bytes(len.value());
  if (!bytes.ok()) return bytes.error();
  return std::string(bytes.value().begin(), bytes.value().end());
}

void write_lp_string(BinaryWriter& w, std::string_view s) {
  w.write_u32(static_cast<std::uint32_t>(s.size()));
  for (char c : s) w.write_u8(static_cast<std::uint8_t>(static_cast<unsigned char>(c)));
}

Result<Hash256Bytes> read_hash(BinaryReader& r) {
  auto b = r.read_bytes(32);
  if (!b.ok()) return b.error();
  Hash256Bytes h{};
  std::copy(b.value().begin(), b.value().end(), h.begin());
  return h;
}

void write_hash(BinaryWriter& w, const Hash256Bytes& h) {
  for (auto b : h) w.write_u8(b);
}

}  // namespace

std::uint32_t payload_checksum(std::span<const std::uint8_t> payload) {
  auto d = sha256d(payload);
  std::uint32_t c = 0;
  for (int i = 0; i < 4; ++i) c |= static_cast<std::uint32_t>(d[static_cast<std::size_t>(i)]) << (i * 8);
  return c;
}

std::vector<std::uint8_t> encode_message(const Message& msg) {
  BinaryWriter out;
  out.write_u32(msg.network_magic);
  out.write_u8(static_cast<std::uint8_t>(msg.type));
  out.write_u8(0);
  out.write_u32(static_cast<std::uint32_t>(msg.payload.size()));
  out.write_u32(payload_checksum(msg.payload));
  auto bytes = out.take();
  bytes.insert(bytes.end(), msg.payload.begin(), msg.payload.end());
  return bytes;
}

Result<Message> decode_message(std::span<const std::uint8_t> bytes, std::uint32_t expected_magic) {
  if (bytes.size() < 14) return MakeError(ErrorCode::kParseError, "message too short");
  BinaryReader r(bytes);
  auto magic = r.read_u32(); if (!magic.ok()) return magic.error();
  auto type = r.read_u8(); if (!type.ok()) return type.error();
  auto reserved = r.read_u8(); if (!reserved.ok()) return reserved.error();
  auto len = r.read_u32(); if (!len.ok()) return len.error();
  auto csum = r.read_u32(); if (!csum.ok()) return csum.error();
  (void)reserved;
  if (magic.value() != expected_magic) return MakeError(ErrorCode::kInvalidArgument, "network magic mismatch");
  if (len.value() > kMaxMessageSize) return MakeError(ErrorCode::kInvalidArgument, "message too large");
  if (r.remaining() < len.value()) return MakeError(ErrorCode::kParseError, "incomplete message payload");
  auto payload_bytes = r.read_bytes(len.value());
  if (!payload_bytes.ok()) return payload_bytes.error();
  if (payload_checksum(payload_bytes.value()) != csum.value()) return MakeError(ErrorCode::kInvalidArgument, "checksum mismatch");
  Message msg;
  msg.network_magic = magic.value();
  msg.type = static_cast<MessageType>(type.value());
  msg.payload = std::move(payload_bytes.value());
  return msg;
}

std::vector<std::uint8_t> encode_hello(const HelloMessage& m) {
  BinaryWriter w;
  w.write_u32(m.network_magic);
  w.write_u32(m.protocol_version);
  w.write_u64(m.nonce);
  w.write_u64(m.timestamp);
  write_hash(w, m.genesis_hash);
  write_lp_string(w, m.agent);
  w.write_u32(static_cast<std::uint32_t>(m.listen_port));
  return w.take();
}

Result<HelloMessage> decode_hello(std::span<const std::uint8_t> payload) {
  BinaryReader r(payload);
  HelloMessage m;
  auto nm = r.read_u32(); if (!nm.ok()) return nm.error();
  auto pv = r.read_u32(); if (!pv.ok()) return pv.error();
  auto nonce = r.read_u64(); if (!nonce.ok()) return nonce.error();
  auto ts = r.read_u64(); if (!ts.ok()) return ts.error();
  auto gh = read_hash(r); if (!gh.ok()) return gh.error();
  auto agent = read_lp_string(r); if (!agent.ok()) return agent.error();
  auto port = r.read_u32(); if (!port.ok()) return port.error();
  m.network_magic = nm.value();
  m.protocol_version = pv.value();
  m.nonce = nonce.value();
  m.timestamp = ts.value();
  m.genesis_hash = gh.value();
  m.agent = agent.value();
  m.listen_port = static_cast<std::uint16_t>(port.value());
  return m;
}

std::vector<std::uint8_t> encode_pingpong(std::uint64_t nonce) {
  BinaryWriter w; w.write_u64(nonce); return w.take();
}

Result<std::uint64_t> decode_pingpong(std::span<const std::uint8_t> payload) {
  BinaryReader r(payload);
  auto n = r.read_u64(); if (!n.ok()) return n.error();
  return n.value();
}

std::vector<std::uint8_t> encode_inv(const std::vector<InventoryItem>& items) {
  BinaryWriter w;
  w.write_u32(static_cast<std::uint32_t>(items.size()));
  for (const auto& it : items) {
    w.write_u8(static_cast<std::uint8_t>(it.type));
    write_hash(w, it.hash);
  }
  return w.take();
}

Result<std::vector<InventoryItem>> decode_inv(std::span<const std::uint8_t> payload) {
  BinaryReader r(payload);
  auto count = r.read_u32(); if (!count.ok()) return count.error();
  if (count.value() > kMaxInventory) return MakeError(ErrorCode::kInvalidArgument, "inv too large");
  std::vector<InventoryItem> items;
  items.reserve(count.value());
  for (std::uint32_t i = 0; i < count.value(); ++i) {
    auto t = r.read_u8(); if (!t.ok()) return t.error();
    auto h = read_hash(r); if (!h.ok()) return h.error();
    items.push_back({static_cast<InventoryType>(t.value()), h.value()});
  }
  return items;
}

std::vector<std::uint8_t> encode_addr(const std::vector<NetAddress>& addrs) {
  BinaryWriter w;
  w.write_u32(static_cast<std::uint32_t>(addrs.size()));
  for (const auto& a : addrs) {
    write_lp_string(w, a.host);
    w.write_u32(a.port);
    w.write_u64(a.services);
    w.write_u64(static_cast<std::uint64_t>(a.last_seen));
  }
  return w.take();
}

Result<std::vector<NetAddress>> decode_addr(std::span<const std::uint8_t> payload) {
  BinaryReader r(payload);
  auto count = r.read_u32(); if (!count.ok()) return count.error();
  if (count.value() > kMaxAddr) return MakeError(ErrorCode::kInvalidArgument, "addr too large");
  std::vector<NetAddress> out;
  out.reserve(count.value());
  for (std::uint32_t i = 0; i < count.value(); ++i) {
    auto host = read_lp_string(r); if (!host.ok()) return host.error();
    auto port = r.read_u32(); if (!port.ok()) return port.error();
    auto services = r.read_u64(); if (!services.ok()) return services.error();
    auto last = r.read_u64(); if (!last.ok()) return last.error();
    NetAddress a;
    a.host = host.value();
    a.port = static_cast<std::uint16_t>(port.value());
    a.services = services.value();
    a.last_seen = static_cast<std::int64_t>(last.value());
    out.push_back(std::move(a));
  }
  return out;
}

std::vector<std::uint8_t> encode_hash(const Hash256Bytes& hash) {
  BinaryWriter w; write_hash(w, hash); return w.take();
}

Result<Hash256Bytes> decode_hash(std::span<const std::uint8_t> payload) {
  BinaryReader r(payload);
  return read_hash(r);
}

std::vector<std::uint8_t> encode_reject(const RejectMessage& m) {
  BinaryWriter w;
  w.write_u8(m.code);
  write_lp_string(w, m.reason);
  return w.take();
}


Result<RejectMessage> decode_reject(std::span<const std::uint8_t> payload) {
  BinaryReader r(payload);
  auto code = r.read_u8();
  if (!code.ok()) return code.error();
  auto reason = read_lp_string(r);
  if (!reason.ok()) return reason.error();
  RejectMessage m;
  m.code = code.value();
  m.reason = reason.value();
  return m;
}

std::vector<std::uint8_t> encode_bft_vote(const consensus::Vote& vote) {
  auto bytes = consensus::serialize_vote(vote);
  bytes.insert(bytes.end(), vote.signature.begin(), vote.signature.end());
  return bytes;
}

Result<consensus::Vote> decode_bft_vote(std::span<const std::uint8_t> payload) {
  constexpr std::size_t kSignatureSize = 64;
  if (payload.size() <= kSignatureSize) return MakeError(ErrorCode::kParseError, "invalid bft vote size");
  const auto canonical_size = payload.size() - kSignatureSize;
  auto vote = consensus::deserialize_vote(payload.first(canonical_size));
  if (!vote.ok()) return vote.error();
  std::copy(payload.begin() + static_cast<std::ptrdiff_t>(canonical_size), payload.end(), vote.value().signature.begin());
  return vote;
}

std::vector<std::uint8_t> encode_bft_proposal(const consensus::BlockProposal& p) { return consensus::serialize_proposal(p); }
Result<consensus::BlockProposal> decode_bft_proposal(std::span<const std::uint8_t> p) { return consensus::deserialize_proposal(p); }
std::vector<std::uint8_t> encode_bft_qc(const consensus::QuorumCertificate& q) { return consensus::serialize_qc(q); }
Result<consensus::QuorumCertificate> decode_bft_qc(std::span<const std::uint8_t> p) { return consensus::deserialize_qc(p); }

std::vector<std::uint8_t> encode_bft_finality(const consensus::FinalityCertificate& c) {
  BinaryWriter w; w.write_u32(c.height);
  for (const auto b : c.block_hash) w.write_u8(b);
  auto qc = consensus::serialize_qc(c.precommit_qc);
  w.write_u32(static_cast<std::uint32_t>(qc.size()));
  for (const auto b : qc) w.write_u8(b);
  return w.take();
}
Result<consensus::FinalityCertificate> decode_bft_finality(std::span<const std::uint8_t> bytes) {
  BinaryReader r(bytes); consensus::FinalityCertificate c; auto h=r.read_u32(); if(!h.ok()) return h.error(); c.height=h.value(); auto bh=r.read_bytes(32); if(!bh.ok()) return bh.error(); std::copy(bh.value().begin(),bh.value().end(),c.block_hash.begin()); auto n=r.read_u32(); if(!n.ok()||n.value()>r.remaining()) return MakeError(ErrorCode::kInvalidArgument,"invalid finality payload"); auto q=r.read_bytes(n.value()); if(!q.ok()) return q.error(); if(!r.eof()) return MakeError(ErrorCode::kInvalidArgument,"trailing finality payload"); auto qc=consensus::deserialize_qc(q.value()); if(!qc.ok()) return qc.error(); c.precommit_qc=qc.value(); return c;
}

std::vector<std::uint8_t> encode_bft_view_change(const consensus::ViewChange& vc) {
  auto bytes = consensus::serialize_view_change(vc);
  bytes.insert(bytes.end(), vc.signature.begin(), vc.signature.end());
  return bytes;
}

Result<consensus::ViewChange> decode_bft_view_change(std::span<const std::uint8_t> payload) {
  constexpr std::size_t kSignatureSize = 64;
  if (payload.size() <= kSignatureSize) return MakeError(ErrorCode::kParseError, "invalid bft view change size");
  const auto canonical_size = payload.size() - kSignatureSize;
  auto vc = consensus::deserialize_view_change(payload.first(canonical_size));
  if (!vc.ok()) return vc.error();
  std::copy(payload.begin() + static_cast<std::ptrdiff_t>(canonical_size), payload.end(), vc.value().signature.begin());
  return vc;
}

std::vector<std::uint8_t> encode_bft_new_view(const consensus::NewView& nv) {
  return consensus::serialize_new_view(nv);
}
Result<consensus::NewView> decode_bft_new_view(std::span<const std::uint8_t> payload) {
  return consensus::deserialize_new_view(payload);
}

}  // namespace phantomx::network
