#include "phantomx/poi/challenge/challenge.hpp"

#include <algorithm>

#include "phantomx/common/serialization.hpp"

namespace phantomx::poi {

std::vector<std::uint8_t> serialize_challenge(const Challenge& c) {
  BinaryWriter w;
  w.write_u8(static_cast<std::uint8_t>(c.kind));
  w.write_bytes(std::span<const std::uint8_t>(c.nonce.data(), c.nonce.size()));
  w.write_u64(c.issued_at);
  w.write_u64(c.expires_at);
  w.write_bytes(std::span<const std::uint8_t>(c.verifier_pubkey.data(), c.verifier_pubkey.size()));
  w.write_bytes(std::span<const std::uint8_t>(c.subject_pubkey.data(), c.subject_pubkey.size()));
  w.write_u32(c.payload_size);
  w.write_u32(c.probe_count);
  w.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(c.region.data()), c.region.size()));
  return w.take();
}

Hash256Bytes challenge_id(const Challenge& c) {
  auto bytes = serialize_challenge(c);
  return sha256d(bytes);
}

bool NonceRegistry::mark_used(const std::array<std::uint8_t, 32>& nonce, std::uint64_t now, std::uint64_t ttl_seconds) {
  prune(now, ttl_seconds);
  for (const auto& e : seen_) if (e.nonce == nonce) return false;
  seen_.push_back({nonce, now});
  return true;
}

void NonceRegistry::prune(std::uint64_t now, std::uint64_t ttl_seconds) {
  seen_.erase(std::remove_if(seen_.begin(), seen_.end(), [&](const Entry& e) {
    return now > e.seen_at && (now - e.seen_at) > ttl_seconds;
  }), seen_.end());
}

Result<Challenge> make_random_challenge(EvidenceKind kind, const crypto::PublicKey& verifier,
                                        const crypto::PublicKey& subject, std::uint64_t now,
                                        std::uint32_t ttl_seconds) {
  Challenge c;
  c.kind = kind;
  auto rnd = crypto::random_bytes(std::span<std::uint8_t>(c.nonce.data(), c.nonce.size()));
  if (!rnd.ok()) return rnd.error();
  c.issued_at = now;
  c.expires_at = now + ttl_seconds;
  c.verifier_pubkey = verifier.bytes;
  c.subject_pubkey = subject.bytes;
  c.payload_size = (kind == EvidenceKind::kBandwidth) ? 1024 * 1024 : 0;
  c.probe_count = (kind == EvidenceKind::kLatency) ? 5 : 1;
  return c;
}

}  // namespace phantomx::poi
