#include "phantomx/poi/uptime_proof.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <sodium.h>

#include "phantomx/common/serialization.hpp"

namespace phantomx::poi {
namespace {

std::array<std::uint8_t, 32> hash_domain(const char* domain, std::span<const std::uint8_t> body) {
  crypto_hash_sha256_state st;
  crypto_hash_sha256_init(&st);
  const auto dlen = std::strlen(domain);
  crypto_hash_sha256_update(&st, reinterpret_cast<const unsigned char*>(domain), dlen);
  crypto_hash_sha256_update(&st, reinterpret_cast<const unsigned char*>(body.data()), body.size());
  std::array<std::uint8_t, 32> out{};
  crypto_hash_sha256_final(&st, reinterpret_cast<unsigned char*>(out.data()));
  return out;
}

std::vector<std::uint8_t> canonical_challenge(const PoiChallengeV1& c) {
  BinaryWriter w;
  w.write_u8(c.version);
  w.write_u64(c.epoch);
  w.write_u64(c.slot);
  w.write_bytes(std::span<const std::uint8_t>(c.verifier_pubkey.data(), c.verifier_pubkey.size()));
  w.write_bytes(std::span<const std::uint8_t>(c.machine_id.data(), c.machine_id.size()));
  w.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(c.owner_address.data()), c.owner_address.size()));
  w.write_bytes(std::span<const std::uint8_t>(c.nonce.data(), c.nonce.size()));
  w.write_u64(c.issued_at_ms);
  w.write_u64(c.expires_at_ms);
  w.write_u8(c.challenge_type);
  w.write_bytes(std::span<const std::uint8_t>(c.params_hash.data(), c.params_hash.size()));
  return w.take();
}

std::vector<std::uint8_t> canonical_response(const PoiResponseV1& r) {
  BinaryWriter w;
  w.write_u8(r.version);
  w.write_bytes(std::span<const std::uint8_t>(r.challenge_id.data(), r.challenge_id.size()));
  w.write_u64(r.epoch);
  w.write_u64(r.slot);
  w.write_bytes(std::span<const std::uint8_t>(r.machine_id.data(), r.machine_id.size()));
  w.write_bytes(std::span<const std::uint8_t>(r.machine_pubkey.data(), r.machine_pubkey.size()));
  w.write_bytes(std::span<const std::uint8_t>(r.response_hash.data(), r.response_hash.size()));
  w.write_u64(r.responded_at_ms);
  return w.take();
}

std::vector<std::uint8_t> canonical_attestation(const PoiAttestationV1& a) {
  BinaryWriter w;
  w.write_u8(a.version);
  w.write_bytes(std::span<const std::uint8_t>(a.challenge_id.data(), a.challenge_id.size()));
  w.write_u64(a.epoch);
  w.write_u64(a.slot);
  w.write_bytes(std::span<const std::uint8_t>(a.machine_id.data(), a.machine_id.size()));
  w.write_bytes(std::span<const std::uint8_t>(a.verifier_pubkey.data(), a.verifier_pubkey.size()));
  w.write_u8(a.pass ? 1 : 0);
  w.write_u32(a.score_uptime_milli);
  w.write_u64(a.observed_at_ms);
  w.write_bytes(std::span<const std::uint8_t>(a.evidence_hash.data(), a.evidence_hash.size()));
  return w.take();
}

std::string hex_key(const std::array<std::uint8_t, 32>& k) {
  static const char* hex="0123456789abcdef"; std::string s; s.resize(64); for(size_t i=0;i<32;++i){s[i*2]=hex[k[i]>>4]; s[i*2+1]=hex[k[i]&0xF];} return s;
}

Result<void> verify_sig(const std::array<std::uint8_t, 32>& hash, const std::array<std::uint8_t, 64>& sig, const std::array<std::uint8_t, 32>& pub) {
  const int ok = crypto_sign_verify_detached(reinterpret_cast<const unsigned char*>(sig.data()),
                                             reinterpret_cast<const unsigned char*>(hash.data()), hash.size(),
                                             reinterpret_cast<const unsigned char*>(pub.data()));
  if (ok != 0) return MakeError(ErrorCode::kUnauthorized, "invalid signature");
  return Result<void>{};
}

}  // namespace

std::array<std::uint8_t, 32> poi_challenge_signing_hash(const PoiChallengeV1& c) {
  const auto body = canonical_challenge(c);
  return hash_domain("PHX/poi/challenge/v1", std::span<const std::uint8_t>(body.data(), body.size()));
}
std::array<std::uint8_t, 32> poi_response_signing_hash(const PoiResponseV1& r) {
  const auto body = canonical_response(r);
  return hash_domain("PHX/poi/response/v1", std::span<const std::uint8_t>(body.data(), body.size()));
}
std::array<std::uint8_t, 32> poi_attestation_signing_hash(const PoiAttestationV1& a) {
  const auto body = canonical_attestation(a);
  return hash_domain("PHX/poi/attestation/v1", std::span<const std::uint8_t>(body.data(), body.size()));
}

Result<void> sign_poi_challenge(PoiChallengeV1& c, const crypto::SecretKey& sk) {
  c.challenge_id = poi_challenge_signing_hash(c);
  const auto h = c.challenge_id;
  crypto_sign_detached(reinterpret_cast<unsigned char*>(c.signature.data()), nullptr,
                       reinterpret_cast<const unsigned char*>(h.data()), h.size(),
                       reinterpret_cast<const unsigned char*>(sk.bytes.data()));
  return Result<void>{};
}
Result<void> sign_poi_response(PoiResponseV1& r, const crypto::SecretKey& sk) {
  const auto h = poi_response_signing_hash(r);
  crypto_sign_detached(reinterpret_cast<unsigned char*>(r.signature.data()), nullptr,
                       reinterpret_cast<const unsigned char*>(h.data()), h.size(),
                       reinterpret_cast<const unsigned char*>(sk.bytes.data()));
  return Result<void>{};
}
Result<void> sign_poi_attestation(PoiAttestationV1& a, const crypto::SecretKey& sk) {
  const auto h = poi_attestation_signing_hash(a);
  crypto_sign_detached(reinterpret_cast<unsigned char*>(a.signature.data()), nullptr,
                       reinterpret_cast<const unsigned char*>(h.data()), h.size(),
                       reinterpret_cast<const unsigned char*>(sk.bytes.data()));
  return Result<void>{};
}

Result<void> verify_poi_challenge(const PoiChallengeV1& c, std::uint64_t now_ms) {
  if (c.version != kPoiUptimeProofVersion) return MakeError(ErrorCode::kInvalidArgument, "bad challenge version");
  if (c.challenge_type != kPoiChallengeTypeUptime) return MakeError(ErrorCode::kInvalidArgument, "bad challenge type");
  if (c.expires_at_ms < c.issued_at_ms) return MakeError(ErrorCode::kInvalidArgument, "bad challenge expiry");
  if (now_ms + kPoiUptimeMaxSkewMs < c.issued_at_ms) return MakeError(ErrorCode::kInvalidArgument, "challenge from future");
  if (now_ms > c.expires_at_ms + kPoiUptimeMaxSkewMs) return MakeError(ErrorCode::kInvalidArgument, "challenge expired");
  const auto expected = poi_challenge_signing_hash(c);
  if (expected != c.challenge_id) return MakeError(ErrorCode::kInvalidArgument, "challenge_id mismatch");
  return verify_sig(expected, c.signature, c.verifier_pubkey);
}

Result<void> verify_poi_response(const PoiResponseV1& r, const PoiChallengeV1& c, std::uint64_t now_ms) {
  if (r.version != kPoiUptimeProofVersion) return MakeError(ErrorCode::kInvalidArgument, "bad response version");
  if (r.challenge_id != c.challenge_id) return MakeError(ErrorCode::kInvalidArgument, "challenge_id mismatch");
  if (r.epoch != c.epoch || r.slot != c.slot || r.machine_id != c.machine_id) return MakeError(ErrorCode::kInvalidArgument, "response binding mismatch");
  if (r.responded_at_ms + kPoiUptimeMaxSkewMs < c.issued_at_ms) return MakeError(ErrorCode::kInvalidArgument, "response too early");
  if (r.responded_at_ms > c.expires_at_ms + kPoiUptimeMaxSkewMs) return MakeError(ErrorCode::kInvalidArgument, "response too late");
  if (now_ms + kPoiUptimeMaxSkewMs < r.responded_at_ms) return MakeError(ErrorCode::kInvalidArgument, "response from future");
  const auto h = poi_response_signing_hash(r);
  return verify_sig(h, r.signature, r.machine_pubkey);
}

Result<void> verify_poi_attestation(const PoiAttestationV1& a, const PoiChallengeV1& c, std::uint64_t now_ms) {
  if (a.version != kPoiUptimeProofVersion) return MakeError(ErrorCode::kInvalidArgument, "bad attestation version");
  if (a.challenge_id != c.challenge_id) return MakeError(ErrorCode::kInvalidArgument, "challenge_id mismatch");
  if (a.epoch != c.epoch || a.slot != c.slot || a.machine_id != c.machine_id) return MakeError(ErrorCode::kInvalidArgument, "attestation binding mismatch");
  if (a.score_uptime_milli > 100000) return MakeError(ErrorCode::kInvalidArgument, "score out of range");
  if (a.observed_at_ms + kPoiUptimeMaxSkewMs < c.issued_at_ms) return MakeError(ErrorCode::kInvalidArgument, "attestation too early");
  if (a.observed_at_ms > c.expires_at_ms + kPoiUptimeMaxSkewMs) return MakeError(ErrorCode::kInvalidArgument, "attestation too late");
  if (now_ms + kPoiUptimeMaxSkewMs < a.observed_at_ms) return MakeError(ErrorCode::kInvalidArgument, "attestation from future");
  const auto h = poi_attestation_signing_hash(a);
  return verify_sig(h, a.signature, a.verifier_pubkey);
}

bool PoiUptimeReplayCache::mark_challenge(const std::array<std::uint8_t, 32>& challenge_id) {
  return seen_.insert("c:" + hex_key(challenge_id)).second;
}
bool PoiUptimeReplayCache::mark_response(const std::array<std::uint8_t, 32>& challenge_id, const std::array<std::uint8_t, 32>& machine_pubkey) {
  return seen_.insert("r:" + hex_key(challenge_id) + ":" + hex_key(machine_pubkey)).second;
}
bool PoiUptimeReplayCache::mark_attestation(const std::array<std::uint8_t, 32>& challenge_id, const std::array<std::uint8_t, 32>& verifier_pubkey) {
  return seen_.insert("a:" + hex_key(challenge_id) + ":" + hex_key(verifier_pubkey)).second;
}
std::size_t PoiUptimeReplayCache::size() const { return seen_.size(); }

PoiUptimeQuorumResult aggregate_poi_uptime_attestations(
    const PoiChallengeV1& challenge,
    const std::vector<PoiAttestationV1>& attestations,
    const std::unordered_set<std::string>& active_verifier_pubkeys_hex,
    std::uint32_t quorum_required,
    std::uint64_t now_ms) {
  PoiUptimeQuorumResult out;
  out.quorum_required = quorum_required;
  std::map<std::string, const PoiAttestationV1*> by_verifier;
  std::vector<std::uint32_t> pass_scores;
  for (const auto& a : attestations) {
    const auto pk_hex = hex_key(a.verifier_pubkey);
    if (!active_verifier_pubkeys_hex.empty() && active_verifier_pubkeys_hex.count(pk_hex) == 0) { ++out.invalid_count; continue; }
    auto v = verify_poi_attestation(a, challenge, now_ms);
    if (!v.ok()) { ++out.invalid_count; continue; }
    auto it = by_verifier.find(pk_hex);
    if (it != by_verifier.end()) {
      const auto& prev = *it->second;
      if (prev.pass != a.pass || prev.score_uptime_milli != a.score_uptime_milli || prev.evidence_hash != a.evidence_hash) {
        ++out.conflict_count;
      } else {
        ++out.duplicate_count;
      }
      continue;
    }
    by_verifier[pk_hex] = &a;
    if (a.pass) { ++out.pass_count; pass_scores.push_back(a.score_uptime_milli); }
    else { ++out.fail_count; }
  }
  out.quorum_met = out.pass_count >= quorum_required && quorum_required > 0;
  if (!pass_scores.empty()) {
    std::sort(pass_scores.begin(), pass_scores.end());
    out.score_uptime_milli = pass_scores[pass_scores.size() / 2];
  }
  return out;
}

}  // namespace phantomx::poi
