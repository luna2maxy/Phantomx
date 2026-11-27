#include "phantomx/rewards/rewards.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <utility>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

#include "phantomx/common/serialization.hpp"
#include "phantomx/primitives/merkle.hpp"

namespace phantomx::rewards {
namespace {

bool is_zero_id(const std::array<std::uint8_t, 32>& id) {
  return std::all_of(id.begin(), id.end(), [](std::uint8_t b) { return b == 0; });
}

Hash256Bytes hash_pair(const Hash256Bytes& a, const Hash256Bytes& b) {
  std::array<std::uint8_t, 64> buf{};
  std::copy(a.begin(), a.end(), buf.begin());
  std::copy(b.begin(), b.end(), buf.begin() + 32);
  return sha256d(std::span<const std::uint8_t>(buf.data(), buf.size()));
}

std::uint64_t mul_div_u64(std::uint64_t a, std::uint64_t b, std::uint64_t c) {
  if (c == 0) return 0;
#if defined(__SIZEOF_INT128__)
  return static_cast<std::uint64_t>((static_cast<__uint128_t>(a) * static_cast<__uint128_t>(b)) / static_cast<__uint128_t>(c));
#elif defined(_MSC_VER) && defined(_M_X64)
  std::uint64_t high = 0;
  std::uint64_t low = _umul128(a, b, &high);
  std::uint64_t rem = 0;
  return _udiv128(high, low, c, &rem);
#else
  // Fallback: split 64-bit multiply to avoid overflow.
  const std::uint64_t a1 = a >> 32;
  const std::uint64_t a0 = a & 0xFFFFFFFFULL;
  const std::uint64_t b1 = b >> 32;
  const std::uint64_t b0 = b & 0xFFFFFFFFULL;
  // (a*b) = a1*b1*2^64 + (a1*b0+a0*b1)*2^32 + a0*b0
  // Compute via long division in 128-bit parts represented as hi/lo.
  const std::uint64_t p0 = a0 * b0;
  const std::uint64_t p1 = a1 * b0;
  const std::uint64_t p2 = a0 * b1;
  const std::uint64_t p3 = a1 * b1;
  const std::uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFFULL) + (p2 & 0xFFFFFFFFULL);
  const std::uint64_t hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
  const std::uint64_t lo = (mid << 32) | (p0 & 0xFFFFFFFFULL);
  // 128-bit / 64-bit division
  if (hi == 0) return lo / c;
  // bitwise long division
  std::uint64_t q = 0;
  std::uint64_t r = 0;
  for (int i = 127; i >= 0; --i) {
    r = (r << 1) | ((i >= 64 ? (hi >> (i - 64)) : (lo >> i)) & 1ULL);
    if (r >= c) {
      r -= c;
      if (i < 64) q |= (1ULL << i);
    }
  }
  return q;
#endif
}

std::vector<EpochParticipant> canonicalize(std::vector<EpochParticipant> v) {
  v.erase(std::remove_if(v.begin(), v.end(), [](const EpochParticipant& p) {
    return p.work_units == 0 || is_zero_id(p.id);
  }), v.end());
  std::sort(v.begin(), v.end(), [](const EpochParticipant& a, const EpochParticipant& b) { return a.id < b.id; });
  // merge duplicates deterministically
  std::vector<EpochParticipant> out;
  for (const auto& p : v) {
    if (!out.empty() && out.back().id == p.id) {
      out.back().work_units += p.work_units; // caller should ensure no overflow in policy
    } else {
      out.push_back(p);
    }
  }
  return out;
}

void distribute_category(std::vector<RewardEntry>& out, const std::vector<EpochParticipant>& participants,
                         std::uint64_t category_atoms, const std::string& category) {
  if (category_atoms == 0 || participants.empty()) return;
  std::uint64_t total_work = 0;
  for (const auto& p : participants) total_work += p.work_units;
  if (total_work == 0) return;

  std::uint64_t distributed = 0;
  for (const auto& p : participants) {
    // floor(proportional share) - deterministic
    std::uint64_t amount = mul_div_u64(category_atoms, p.work_units, total_work);
    if (amount == 0) continue;
    out.push_back({p.id, amount, category});
    distributed += amount;
  }
  // deterministic remainder: assign to first canonical participant
  if (distributed < category_atoms && !out.empty()) {
    // find first entry in this category (canonical order preserved)
    for (auto& e : out) {
      if (e.category == category) {
        e.amount_atoms += (category_atoms - distributed);
        break;
      }
    }
  } else if (distributed < category_atoms && !participants.empty()) {
    out.push_back({participants.front().id, category_atoms - distributed, category});
  }
}

}  // namespace

std::uint64_t block_subsidy_atoms(std::uint64_t height) {
  const std::uint64_t halvings = height / kHalvingIntervalBlocks;
  if (halvings >= 64) return 0;
  return kInitialBlockRewardAtoms >> halvings;
}

std::uint64_t total_minted_atoms_through_height(std::uint64_t height_inclusive) {
  // Sum subsidy for heights [0, height_inclusive]
  std::uint64_t total = 0;
  std::uint64_t h = 0;
  while (h <= height_inclusive) {
    const std::uint64_t halvings = h / kHalvingIntervalBlocks;
    if (halvings >= 64) break;
    const std::uint64_t subsidy = kInitialBlockRewardAtoms >> halvings;
    if (subsidy == 0) break;
    const std::uint64_t next_boundary = (halvings + 1) * kHalvingIntervalBlocks;
    const std::uint64_t end = std::min(height_inclusive + 1, next_boundary);
    const std::uint64_t count = end - h;
    const std::uint64_t max_add = kTheoreticalMaxSupplyAtoms - total;
    const std::uint64_t add = (count > 0 && subsidy > max_add / count) ? max_add : subsidy * count;
    total += add;
    if (total >= kTheoreticalMaxSupplyAtoms) return kTheoreticalMaxSupplyAtoms;
    h = end;
  }
  return std::min(total, kTheoreticalMaxSupplyAtoms);
}

std::vector<RewardEntry> calculate_epoch_rewards(const EpochSnapshot& snapshot) {
  std::vector<RewardEntry> out;
  const std::uint64_t total = snapshot.total_subsidy_atoms;
  if (total == 0) return out;

  // Integer BPS split: floor for verifier/pool, PoI gets remainder to ensure sum == total.
  const std::uint64_t verifier_atoms = (total * kVerifierShareBps) / kBpsDenom;
  const std::uint64_t pool_atoms = (total * kPoolShareBps) / kBpsDenom;
  const std::uint64_t poi_atoms = total - verifier_atoms - pool_atoms;

  auto poi = canonicalize(snapshot.poi_contributors);
  auto ver = canonicalize(snapshot.verifier_operators);
  auto pool = canonicalize(snapshot.pool_operators);

  distribute_category(out, poi, poi_atoms, "poi");
  distribute_category(out, ver, verifier_atoms, "verifier");
  distribute_category(out, pool, pool_atoms, "pool");

  // Canonical final ordering for deterministic commitment
  std::sort(out.begin(), out.end(), [](const RewardEntry& a, const RewardEntry& b) {
    if (a.category != b.category) return a.category < b.category;
    if (a.recipient_id != b.recipient_id) return a.recipient_id < b.recipient_id;
    return a.amount_atoms < b.amount_atoms;
  });
  return out;
}

Hash256Bytes reward_leaf_hash(const RewardEntry& e) {
  BinaryWriter w;
  w.write_bytes(std::span<const std::uint8_t>(e.recipient_id.data(), e.recipient_id.size()));
  w.write_u64(e.amount_atoms);
  w.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(e.category.data()), e.category.size()));
  auto bytes = w.take();
  const std::string domain = "PHX/reward/leaf/v1";
  std::vector<std::uint8_t> pre(domain.begin(), domain.end());
  pre.insert(pre.end(), bytes.begin(), bytes.end());
  return sha256d(pre);
}

std::vector<ClaimProofStep> build_reward_merkle_proof(const std::vector<Hash256Bytes>& leaves, std::size_t index) {
  std::vector<ClaimProofStep> proof;
  if (leaves.empty() || index >= leaves.size()) return proof;
  std::vector<Hash256Bytes> level = leaves;
  std::size_t idx = index;
  while (level.size() > 1) {
    if (level.size() % 2 == 1) level.push_back(level.back());
    std::size_t sibling = (idx % 2 == 0) ? idx + 1 : idx - 1;
    ClaimProofStep step;
    step.hash = level[sibling];
    step.is_left = (idx % 2 == 1);
    proof.push_back(step);
    std::vector<Hash256Bytes> next;
    next.reserve(level.size() / 2);
    for (std::size_t i = 0; i < level.size(); i += 2) next.push_back(hash_pair(level[i], level[i + 1]));
    level = std::move(next);
    idx /= 2;
  }
  return proof;
}

bool verify_reward_merkle_proof(const Hash256Bytes& leaf, const std::vector<ClaimProofStep>& proof, const Hash256Bytes& root) {
  Hash256Bytes cur = leaf;
  for (const auto& step : proof) {
    cur = step.is_left ? hash_pair(step.hash, cur) : hash_pair(cur, step.hash);
  }
  return cur == root;
}

RewardCommitment build_reward_commitment(const EpochSnapshot& snapshot) {
  RewardCommitment c;
  c.epoch = snapshot.epoch;
  c.entries = calculate_epoch_rewards(snapshot);
  std::vector<Hash256Bytes> leaves;
  leaves.reserve(c.entries.size());
  std::uint64_t total = 0;
  for (const auto& e : c.entries) {
    leaves.push_back(reward_leaf_hash(e));
    total += e.amount_atoms;
  }
  c.total_reward_atoms = total;
  c.merkle_root = merkle_root(leaves);
  return c;
}

bool verify_claim(const RewardCommitment& commitment, const RewardClaim& claim) {
  if (claim.epoch != commitment.epoch) return false;
  RewardEntry e{claim.recipient_id, claim.amount_atoms, claim.category};
  const auto leaf = reward_leaf_hash(e);
  return verify_reward_merkle_proof(leaf, claim.merkle_proof, commitment.merkle_root);
}

std::string ClaimRegistry::key(std::uint64_t epoch, const std::array<std::uint8_t, 32>& recipient_id, const std::string& category) {
  return std::to_string(epoch) + ":" + to_hex(std::span<const std::uint8_t>(recipient_id.data(), recipient_id.size())) + ":" + category;
}

bool ClaimRegistry::has_claimed(std::uint64_t epoch, const std::array<std::uint8_t, 32>& recipient_id, const std::string& category) const {
  return claimed_.find(key(epoch, recipient_id, category)) != claimed_.end();
}

Result<void> ClaimRegistry::mark_claimed(std::uint64_t epoch, const std::array<std::uint8_t, 32>& recipient_id, const std::string& category) {
  auto k = key(epoch, recipient_id, category);
  if (claimed_.find(k) != claimed_.end()) return MakeError(ErrorCode::kInvalidArgument, "double claim");
  claimed_.insert(std::move(k));
  return Result<void>{};
}

}  // namespace phantomx::rewards
