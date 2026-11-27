#include <array>
#include <vector>

#include "../test_framework.hpp"
#include "phantomx/common/types.hpp"
#include "phantomx/rewards/rewards.hpp"

namespace {
std::array<std::uint8_t, 32> id(std::uint8_t b) {
  std::array<std::uint8_t, 32> out{};
  out[0] = b;
  return out;
}
}  // namespace

PHX_TEST(rewards_halving_schedule) {
  PHX_EXPECT(phantomx::rewards::block_subsidy_atoms(0) == 2500000000ULL);
  PHX_EXPECT(phantomx::rewards::block_subsidy_atoms(2099999) == 2500000000ULL); // before halving
  PHX_EXPECT(phantomx::rewards::block_subsidy_atoms(2100000) == 1250000000ULL); // exact halving
  PHX_EXPECT(phantomx::rewards::block_subsidy_atoms(2100001) == 1250000000ULL); // after halving
  PHX_EXPECT(phantomx::rewards::block_subsidy_atoms(4200000) == 625000000ULL);  // second halving
  PHX_EXPECT(phantomx::rewards::block_subsidy_atoms(6300000) == 312500000ULL);  // third halving
}

PHX_TEST(rewards_repeated_halving_to_zero) {
  // after enough halvings, subsidy becomes 0
  std::uint64_t h = 0;
  std::uint64_t subsidy = phantomx::rewards::block_subsidy_atoms(0);
  for (int i = 0; i < 64 && subsidy > 0; ++i) {
    h += phantomx::kHalvingIntervalBlocks;
    subsidy = phantomx::rewards::block_subsidy_atoms(h);
  }
  PHX_EXPECT(subsidy == 0);
  PHX_EXPECT(phantomx::rewards::block_subsidy_atoms(h + phantomx::kHalvingIntervalBlocks) == 0);
}

PHX_TEST(rewards_max_supply_cap) {
  // total minted should not exceed theoretical max supply; integer halving truncation means
  // actual mintable is slightly below the nominal 105,000,000 PHX.
  auto total = phantomx::rewards::total_minted_atoms_through_height(100000000ULL);
  PHX_EXPECT(total <= phantomx::kTheoreticalMaxSupplyAtoms);
  PHX_EXPECT(total == 10499999976900000ULL);
}

PHX_TEST(rewards_distribution_70_20_10_and_rounding) {
  phantomx::rewards::EpochSnapshot snap;
  snap.epoch = 1;
  snap.total_subsidy_atoms = 2500000000ULL;
  snap.poi_contributors.push_back({id(1), 1});
  snap.poi_contributors.push_back({id(2), 1});
  snap.verifier_operators.push_back({id(3), 1});
  snap.pool_operators.push_back({id(4), 1});
  auto entries = phantomx::rewards::calculate_epoch_rewards(snap);
  std::uint64_t poi = 0, ver = 0, pool = 0, total = 0;
  for (const auto& e : entries) {
    total += e.amount_atoms;
    if (e.category == "poi") poi += e.amount_atoms;
    if (e.category == "verifier") ver += e.amount_atoms;
    if (e.category == "pool") pool += e.amount_atoms;
  }
  PHX_EXPECT(total == snap.total_subsidy_atoms);
  PHX_EXPECT(poi == 1750000000ULL);
  PHX_EXPECT(ver == 500000000ULL);
  PHX_EXPECT(pool == 250000000ULL);
}

PHX_TEST(rewards_rounding_no_atom_loss) {
  phantomx::rewards::EpochSnapshot snap;
  snap.epoch = 2;
  snap.total_subsidy_atoms = 10; // tiny amount to force rounding
  snap.poi_contributors.push_back({id(1), 1});
  snap.poi_contributors.push_back({id(2), 1});
  snap.poi_contributors.push_back({id(3), 1});
  snap.verifier_operators.push_back({id(4), 1});
  snap.pool_operators.push_back({id(5), 1});
  auto entries = phantomx::rewards::calculate_epoch_rewards(snap);
  std::uint64_t total = 0;
  for (const auto& e : entries) total += e.amount_atoms;
  PHX_EXPECT(total == 10ULL);
}

PHX_TEST(rewards_claim_commitment_and_double_claim) {
  phantomx::rewards::EpochSnapshot snap;
  snap.epoch = 3;
  snap.total_subsidy_atoms = 2500000000ULL;
  snap.poi_contributors.push_back({id(1), 100});
  snap.verifier_operators.push_back({id(2), 100});
  snap.pool_operators.push_back({id(3), 100});
  auto commitment = phantomx::rewards::build_reward_commitment(snap);
  PHX_EXPECT(!commitment.entries.empty());
  PHX_EXPECT(commitment.total_reward_atoms == snap.total_subsidy_atoms);

  // find first entry and build proof
  std::vector<phantomx::Hash256Bytes> leaves;
  for (const auto& e : commitment.entries) leaves.push_back(phantomx::rewards::reward_leaf_hash(e));
  std::size_t idx = 0;
  for (std::size_t i = 0; i < commitment.entries.size(); ++i) {
    if (commitment.entries[i].category == "poi") { idx = i; break; }
  }
  phantomx::rewards::RewardClaim claim;
  claim.epoch = commitment.epoch;
  claim.recipient_id = commitment.entries[idx].recipient_id;
  claim.amount_atoms = commitment.entries[idx].amount_atoms;
  claim.category = commitment.entries[idx].category;
  claim.merkle_proof = phantomx::rewards::build_reward_merkle_proof(leaves, idx);
  PHX_EXPECT(phantomx::rewards::verify_claim(commitment, claim));

  phantomx::rewards::ClaimRegistry reg;
  PHX_EXPECT(reg.mark_claimed(claim.epoch, claim.recipient_id, claim.category).ok());
  PHX_EXPECT(!reg.mark_claimed(claim.epoch, claim.recipient_id, claim.category).ok()); // double claim

  // invalid claim - tampered amount
  auto bad = claim;
  bad.amount_atoms += 1;
  PHX_EXPECT(!phantomx::rewards::verify_claim(commitment, bad));
}

PHX_TEST(rewards_zero_subsidy_epoch) {
  phantomx::rewards::EpochSnapshot snap;
  snap.epoch = 9;
  snap.total_subsidy_atoms = 0;
  snap.poi_contributors.push_back({id(1), 100});
  auto entries = phantomx::rewards::calculate_epoch_rewards(snap);
  PHX_EXPECT(entries.empty());
}

