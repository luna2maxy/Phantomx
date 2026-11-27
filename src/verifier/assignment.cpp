#include "phantomx/verifier/assignment.hpp"

#include <algorithm>
#include <numeric>

#include "phantomx/primitives/hash.hpp"

namespace phantomx::verifier {
namespace {
bool is_zero(const std::array<std::uint8_t, 32>& a) {
  return std::all_of(a.begin(), a.end(), [](std::uint8_t b) { return b == 0; });
}

Hash256Bytes hash_assignment(const AssignmentRequest& req, const std::vector<std::array<std::uint8_t, 32>>& verifiers) {
  std::vector<std::uint8_t> buf;
  buf.push_back(static_cast<std::uint8_t>(req.type));
  buf.insert(buf.end(), req.subject_pubkey.begin(), req.subject_pubkey.end());
  buf.insert(buf.end(), req.subject_operator_pubkey.begin(), req.subject_operator_pubkey.end());
  buf.insert(buf.end(), req.pool_operator_pubkey.begin(), req.pool_operator_pubkey.end());
  for (int i = 0; i < 8; ++i) buf.push_back(static_cast<std::uint8_t>((req.epoch >> (i * 8)) & 0xFF));
  buf.insert(buf.end(), req.randomness_seed.begin(), req.randomness_seed.end());
  for (const auto& v : verifiers) buf.insert(buf.end(), v.begin(), v.end());
  return sha256d(buf);
}
}  // namespace

ConflictCheck check_conflict(const VerifierRecord& verifier, const AssignmentRequest& req) {
  if (verifier.verifier_pubkey == req.subject_pubkey) return {false, "self-verification forbidden"};
  if (!is_zero(req.subject_operator_pubkey) && verifier.operator_pubkey == req.subject_operator_pubkey) {
    return {false, "same operator as subject"};
  }
  if (!is_zero(req.pool_operator_pubkey) && verifier.operator_pubkey == req.pool_operator_pubkey) {
    return {false, "pool operator conflict"};
  }
  return {true, ""};
}

Result<AssignmentRecord> assign_verifiers(const VerifierRegistry& registry, const AssignmentRequest& req) {
  if (req.required_verifiers < 2) return MakeError(ErrorCode::kInvalidArgument, "multi-verifier assignment requires >= 2");
  auto candidates = registry.active_verifiers(req.type);
  std::vector<VerifierRecord> eligible;
  for (const auto& c : candidates) {
    auto conflict = check_conflict(c, req);
    if (conflict.ok) eligible.push_back(c);
  }
  if (eligible.size() < req.required_verifiers) return MakeError(ErrorCode::kNotFound, "not enough eligible verifiers");

  // Deterministic auditable ranking by hash(seed|epoch|verifier_pubkey)
  std::vector<std::pair<Hash256Bytes, VerifierRecord>> ranked;
  ranked.reserve(eligible.size());
  for (const auto& v : eligible) {
    std::vector<std::uint8_t> buf;
    buf.insert(buf.end(), req.randomness_seed.begin(), req.randomness_seed.end());
    for (int i = 0; i < 8; ++i) buf.push_back(static_cast<std::uint8_t>((req.epoch >> (i * 8)) & 0xFF));
    buf.insert(buf.end(), v.verifier_pubkey.begin(), v.verifier_pubkey.end());
    ranked.push_back({sha256d(buf), v});
  }
  std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

  AssignmentRecord rec;
  for (std::size_t i = 0; i < ranked.size() && rec.verifiers.size() < req.required_verifiers; ++i) {
    // ensure pool operator is not sole verifier: if only 1 required, still allowed? req says multi-verifier in rules; enforce >=2 at policy layer.
    rec.verifiers.push_back(ranked[i].second.verifier_pubkey);
  }
  rec.assignment_hash = hash_assignment(req, rec.verifiers);
  return rec;
}

}  // namespace phantomx::verifier
