#include "phantomx/poi/evidence/evidence.hpp"

#include <algorithm>

#include "phantomx/common/serialization.hpp"

namespace phantomx::poi {

std::vector<std::uint8_t> serialize_evidence(const EvidenceRecord& e) {
  BinaryWriter w;
  w.write_u8(static_cast<std::uint8_t>(e.kind));
  w.write_bytes(std::span<const std::uint8_t>(e.subject_pubkey.data(), e.subject_pubkey.size()));
  w.write_bytes(std::span<const std::uint8_t>(e.challenge_nonce.data(), e.challenge_nonce.size()));
  w.write_u64(e.window_start);
  w.write_u64(e.window_end);
  w.write_u64(e.measured_value_a);
  w.write_u64(e.measured_value_b);
  w.write_u64(e.measured_value_c);
  w.write_u32(e.component_score);
  w.write_u8(e.simulator ? 1 : 0);
  // attestations are not part of hash preimage for signing; they sign the evidence hash.
  return w.take();
}

Hash256Bytes evidence_hash(const EvidenceRecord& e) {
  const auto bytes = serialize_evidence(e);
  const std::string domain = "PHX/poi/evidence/v1";
  std::vector<std::uint8_t> preimage(domain.begin(), domain.end());
  preimage.insert(preimage.end(), bytes.begin(), bytes.end());
  return sha256d(preimage);
}

Result<VerifierAttestation> sign_evidence(const EvidenceRecord& e, const crypto::SecretKey& verifier_secret,
                                          const crypto::PublicKey& verifier_pub, std::uint64_t timestamp) {
  VerifierAttestation a;
  a.verifier_pubkey = verifier_pub.bytes;
  a.timestamp = timestamp;
  a.evidence_hash = evidence_hash(e);
  auto sig = crypto::sign(std::span<const std::uint8_t>(a.evidence_hash.data(), a.evidence_hash.size()), verifier_secret);
  if (!sig.ok()) return sig.error();
  a.signature = sig.value().bytes;
  return a;
}

bool verify_attestation(const EvidenceRecord& e, const VerifierAttestation& a) {
  const auto h = evidence_hash(e);
  if (h != a.evidence_hash) return false;
  crypto::PublicKey pk{}; pk.bytes = a.verifier_pubkey;
  crypto::Signature sig{}; sig.bytes = a.signature;
  return crypto::verify(std::span<const std::uint8_t>(h.data(), h.size()), sig, pk);
}

bool evidence_has_independent_attestation(const EvidenceRecord& e, std::size_t min_attestations) {
  std::vector<std::array<std::uint8_t, 32>> seen;
  std::size_t valid = 0;
  for (const auto& a : e.attestations) {
    if (!verify_attestation(e, a)) continue;
    if (std::find(seen.begin(), seen.end(), a.verifier_pubkey) != seen.end()) continue;
    seen.push_back(a.verifier_pubkey);
    ++valid;
    if (valid >= min_attestations) return true;
  }
  return false;
}

}  // namespace phantomx::poi
