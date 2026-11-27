#include <array>

#include "../test_framework.hpp"
#include "phantomx/pool/pool.hpp"

namespace {
std::array<std::uint8_t, 32> id(std::uint8_t b) {
  std::array<std::uint8_t, 32> out{};
  out[0] = b;
  return out;
}
}  // namespace

PHX_TEST(pool_register_auth_and_role_separation) {
  phantomx::pool::PoolLedger ledger;
  phantomx::pool::WorkerRecord w;
  w.worker_id = id(1); w.operator_id = id(9); w.role = phantomx::pool::PoolRole::kPoiWorker; w.auth_token_hash = "hash1";
  PHX_EXPECT(ledger.register_worker(w, 100).ok());
  PHX_EXPECT(!ledger.register_worker(w, 101).ok()); // duplicate
  PHX_EXPECT(ledger.authenticate(id(1), "hash1").ok());
  PHX_EXPECT(!ledger.authenticate(id(1), "bad").ok());
}

PHX_TEST(pool_no_duplicate_shares_and_role_mismatch) {
  phantomx::pool::PoolLedger ledger;
  phantomx::pool::WorkerRecord w;
  w.worker_id = id(2); w.operator_id = id(9); w.role = phantomx::pool::PoolRole::kVerifierWorker; w.auth_token_hash = "h";
  PHX_EXPECT(ledger.register_worker(w, 100).ok());

  phantomx::pool::ShareRecord s;
  s.worker_id = id(2); s.role = phantomx::pool::PoolRole::kVerifierWorker; s.epoch = 1; s.units = 10; s.evidence_hash_hex = "abc";
  PHX_EXPECT(ledger.submit_share(s).ok());
  PHX_EXPECT(!ledger.submit_share(s).ok()); // duplicate share

  auto bad = s; bad.role = phantomx::pool::PoolRole::kPoiWorker; bad.evidence_hash_hex = "def";
  PHX_EXPECT(!ledger.submit_share(bad).ok()); // role mismatch
}

PHX_TEST(pool_no_duplicate_payout_and_estimation) {
  phantomx::pool::PoolLedger ledger;
  phantomx::pool::WorkerRecord w1; w1.worker_id = id(1); w1.operator_id = id(9); w1.role = phantomx::pool::PoolRole::kPoiWorker; w1.auth_token_hash = "h1";
  phantomx::pool::WorkerRecord w2; w2.worker_id = id(2); w2.operator_id = id(9); w2.role = phantomx::pool::PoolRole::kPoiWorker; w2.auth_token_hash = "h2";
  PHX_EXPECT(ledger.register_worker(w1, 1).ok());
  PHX_EXPECT(ledger.register_worker(w2, 1).ok());
  PHX_EXPECT(ledger.submit_share({id(1), phantomx::pool::PoolRole::kPoiWorker, 1, 30, "e1"}).ok());
  PHX_EXPECT(ledger.submit_share({id(2), phantomx::pool::PoolRole::kPoiWorker, 1, 70, "e2"}).ok());

  auto est = ledger.estimate_reward_atoms(id(1), phantomx::pool::PoolRole::kPoiWorker, 1, 1000);
  PHX_EXPECT_EQ(static_cast<long long>(est), 300LL);

  phantomx::pool::PayoutRecord p; p.worker_id = id(1); p.epoch = 1; p.amount_atoms = 300; p.category = "poi";
  PHX_EXPECT(ledger.record_payout(p, 1).ok());
  PHX_EXPECT(!ledger.record_payout(p, 1).ok()); // duplicate payout
  PHX_EXPECT(ledger.has_payout(id(1), 1, "poi"));
}

