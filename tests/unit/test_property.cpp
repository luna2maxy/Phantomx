#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include "../test_framework.hpp"
#include "phantomx/common/serialization.hpp"
#include "phantomx/primitives/transaction.hpp"
#include "phantomx/rewards/rewards.hpp"

PHX_TEST(property_serialization_roundtrip_u64) {
  std::mt19937_64 rng(12345);
  for (int i = 0; i < 200; ++i) {
    std::uint64_t v = rng();
    phantomx::BinaryWriter w; w.write_u64(v);
    phantomx::BinaryReader r(w.buffer());
    auto out = r.read_u64();
    PHX_EXPECT(out.ok());
    PHX_EXPECT(out.value() == v);
  }
}

PHX_TEST(property_reward_invariant_no_overflow_total) {
  std::mt19937_64 rng(42);
  for (int i = 0; i < 100; ++i) {
    phantomx::rewards::EpochSnapshot snap;
    snap.epoch = static_cast<std::uint64_t>(i);
    snap.total_subsidy_atoms = (rng() % 5'000'000'000ULL) + 1;
    for (int j=0;j<3;++j){ phantomx::rewards::EpochParticipant p; p.id.fill(static_cast<std::uint8_t>(rng()&0xff)); p.id[1]=static_cast<std::uint8_t>(j); p.work_units=(rng()%1000)+1; snap.poi_contributors.push_back(p);} 
    for (int j=0;j<2;++j){ phantomx::rewards::EpochParticipant p; p.id.fill(static_cast<std::uint8_t>((rng()>>8)&0xff)); p.id[0]=static_cast<std::uint8_t>(j+10); p.work_units=(rng()%1000)+1; snap.verifier_operators.push_back(p);} 
    for (int j=0;j<1;++j){ phantomx::rewards::EpochParticipant p; p.id.fill(static_cast<std::uint8_t>((rng()>>16)&0xff)); p.id[0]=static_cast<std::uint8_t>(j+20); p.work_units=(rng()%1000)+1; snap.pool_operators.push_back(p);} 
    auto entries = phantomx::rewards::calculate_epoch_rewards(snap);
    std::uint64_t total=0; for (auto& e: entries) total += e.amount_atoms;
    PHX_EXPECT(total <= snap.total_subsidy_atoms);
  }
}
