#include "phantomx/verifier/service.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "phantomx/common/serialization.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::verifier {

std::vector<std::uint8_t> serialize_result(const SignedVerifierResult& r) {
  BinaryWriter w;
  w.write_bytes(std::span<const std::uint8_t>(r.challenge_id.data(), r.challenge_id.size()));
  w.write_bytes(std::span<const std::uint8_t>(r.verifier_pubkey.data(), r.verifier_pubkey.size()));
  w.write_u8(static_cast<std::uint8_t>(r.type));
  w.write_u8(r.accepted ? 1 : 0);
  w.write_u64(r.timestamp);
  w.write_bytes(std::span<const std::uint8_t>(r.evidence_hash.data(), r.evidence_hash.size()));
  return w.take();
}

Hash256Bytes result_hash(const SignedVerifierResult& r) {
  auto bytes = serialize_result(r);
  const std::string domain = "PHX/verifier/result/v1";
  std::vector<std::uint8_t> preimage(domain.begin(), domain.end());
  preimage.insert(preimage.end(), bytes.begin(), bytes.end());
  return sha256d(preimage);
}

Result<SignedVerifierResult> sign_result(SignedVerifierResult r, const crypto::SecretKey& verifier_secret) {
  const auto h = result_hash(r);
  auto sig = crypto::sign(std::span<const std::uint8_t>(h.data(), h.size()), verifier_secret);
  if (!sig.ok()) return sig.error();
  r.signature = sig.value().bytes;
  return r;
}

bool verify_result_signature(const SignedVerifierResult& r) {
  const auto h = result_hash(r);
  crypto::PublicKey pk{}; pk.bytes = r.verifier_pubkey;
  crypto::Signature sig{}; sig.bytes = r.signature;
  return crypto::verify(std::span<const std::uint8_t>(h.data(), h.size()), sig, pk);
}

Result<ChallengeTask> VerifierService::create_challenge(VerifierType type, const std::array<std::uint8_t, 32>& subject,
                                                        const std::array<std::uint8_t, 32>& verifier,
                                                        std::uint64_t now, std::uint32_t ttl_seconds,
                                                        std::vector<std::uint8_t> payload) {
  ChallengeTask t;
  t.type = type;
  t.subject_pubkey = subject;
  t.verifier_pubkey = verifier;
  t.created_at = now;
  t.expires_at = now + ttl_seconds;
  t.payload = std::move(payload);
  // challenge id: hash of fields
  BinaryWriter w;
  w.write_u8(static_cast<std::uint8_t>(type));
  w.write_bytes(std::span<const std::uint8_t>(subject.data(), subject.size()));
  w.write_bytes(std::span<const std::uint8_t>(verifier.data(), verifier.size()));
  w.write_u64(now);
  w.write_u64(t.expires_at);
  w.write_bytes(std::span<const std::uint8_t>(t.payload.data(), t.payload.size()));
  auto bytes = w.take();
  auto h = sha256d(bytes);
  std::copy(h.begin(), h.end(), t.challenge_id.begin());
  return t;
}

Result<void> VerifierService::deliver_challenge(ChallengeTask& task) {
  if (task.challenge_id == std::array<std::uint8_t, 32>{}) return MakeError(ErrorCode::kInvalidArgument, "invalid challenge id");
  task.delivered = true;
  return Result<void>{};
}

Result<SignedVerifierResult> VerifierService::verify_and_sign(const ChallengeTask& task, const poi::EvidenceRecord& evidence,
                                                              bool accepted, const crypto::SecretKey& verifier_secret,
                                                              const crypto::PublicKey& verifier_pub, std::uint64_t now) {
  if (task.verifier_pubkey != verifier_pub.bytes) return MakeError(ErrorCode::kInvalidArgument, "verifier key mismatch");
  if (task.subject_pubkey == verifier_pub.bytes) return MakeError(ErrorCode::kInvalidArgument, "self-verification forbidden");
  if (now > task.expires_at) return MakeError(ErrorCode::kInvalidArgument, "challenge expired");
  if (evidence.subject_pubkey != task.subject_pubkey) return MakeError(ErrorCode::kInvalidArgument, "evidence subject mismatch");
  if (evidence.simulator) return MakeError(ErrorCode::kInvalidArgument, "simulator evidence not allowed");

  SignedVerifierResult r;
  r.challenge_id = task.challenge_id;
  r.verifier_pubkey = verifier_pub.bytes;
  r.type = task.type;
  r.accepted = accepted;
  r.timestamp = now;
  r.evidence_hash = poi::evidence_hash(evidence);
  return sign_result(std::move(r), verifier_secret);
}

Result<DisputeRecord> VerifierService::open_dispute(const SignedVerifierResult& result, const std::array<std::uint8_t, 32>& reporter,
                                                     std::string reason, std::uint64_t now) {
  DisputeRecord d;
  d.challenge_id = result.challenge_id;
  d.reporter_pubkey = reporter;
  d.reason = std::move(reason);
  d.status = DisputeStatus::kOpen;
  d.created_at = now;
  auto h = sha256d(std::span<const std::uint8_t>(result.signature.data(), result.signature.size()));
  std::copy(h.begin(), h.end(), d.dispute_id.begin());
  disputes_.push_back(d);
  return d;
}

Result<void> VerifierService::resolve_dispute(const std::array<std::uint8_t, 32>& dispute_id, DisputeStatus status) {
  for (auto& d : disputes_) {
    if (d.dispute_id == dispute_id) {
      d.status = status;
      return Result<void>{};
    }
  }
  return MakeError(ErrorCode::kNotFound, "dispute not found");
}

}  // namespace phantomx::verifier
