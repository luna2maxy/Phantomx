#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "phantomx/common/config.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/common/serialization.hpp"
#include "phantomx/consensus/bft.hpp"
#include "phantomx/consensus/bft_config.hpp"
#include "phantomx/consensus/consensus.hpp"
#include "phantomx/network/protocol.hpp"
#include "phantomx/primitives/hash.hpp"

namespace {

std::string unique_wal_path(const char* tag) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return (std::filesystem::temp_directory_path() /
          (std::string("phantomx-bft-wal-") + tag + "-" + std::to_string(stamp) + ".bin"))
      .string();
}

phantomx::consensus::ValidatorSet make_vs(const std::vector<phantomx::crypto::KeyPair>& keys) {
  phantomx::consensus::ValidatorSet vs;
  vs.epoch = 0;
  for (const auto& k : keys) {
    phantomx::consensus::Validator v;
    v.validator_pubkey = k.pub.bytes;
    v.operator_pubkey = k.pub.bytes;
    v.voting_power = 10;
    v.active = true;
    vs.validators.push_back(v);
    vs.total_voting_power += v.voting_power;
  }
  return vs;
}

phantomx::Hash256Bytes h(std::uint8_t b) {
  phantomx::Hash256Bytes out{};
  out[0] = b;
  return out;
}

}  // namespace

PHX_TEST(consensus_honest_validators_qc_and_finalize) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  auto block = h(1);
  std::vector<phantomx::consensus::Vote> votes;
  for (int i = 0; i < 3; ++i) { // >2/3 of 4 equal power
    phantomx::consensus::Vote v;
    v.phase = phantomx::consensus::VotePhase::kPrecommit;
    v.epoch = 0; v.view = 0; v.height = 1; v.block_hash = block;
    v.validator_pubkey = keys[static_cast<std::size_t>(i)].pub.bytes;
    v.voting_power = 10;
    auto signed_v = phantomx::consensus::sign_vote(v, keys[static_cast<std::size_t>(i)].sec);
    PHX_EXPECT(signed_v.ok());
    PHX_EXPECT(engine.submit_vote(signed_v.value()).ok());
    votes.push_back(signed_v.value());
  }
  auto qc = phantomx::consensus::build_qc(vs, votes);
  PHX_EXPECT(qc.ok());
  phantomx::consensus::FinalityCertificate cert;
  cert.height = 1; cert.block_hash = block; cert.precommit_qc = qc.value();
  PHX_EXPECT(engine.finalize(cert).ok());
  PHX_EXPECT(engine.state().last_finalized.has_value());
}

PHX_TEST(consensus_byzantine_duplicate_and_equivocation) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  phantomx::consensus::BftEngine engine(make_vs(keys));
  phantomx::consensus::Vote v;
  v.phase = phantomx::consensus::VotePhase::kPrevote;
  v.epoch = 0; v.view = 0; v.height = 1; v.block_hash = h(1);
  v.validator_pubkey = keys[0].pub.bytes; v.voting_power = 10;
  auto s1 = phantomx::consensus::sign_vote(v, keys[0].sec);
  PHX_EXPECT(s1.ok());
  PHX_EXPECT(engine.submit_vote(s1.value()).ok());
  PHX_EXPECT(engine.submit_vote(s1.value()).ok()); // duplicate same vote ok

  auto v2 = v; v2.block_hash = h(2);
  auto s2 = phantomx::consensus::sign_vote(v2, keys[0].sec);
  PHX_EXPECT(s2.ok());
  PHX_EXPECT(!engine.submit_vote(s2.value()).ok()); // equivocation
  PHX_EXPECT_EQ(static_cast<long long>(engine.equivocations().size()), 1LL);
}

PHX_TEST(consensus_conflicting_proposal_finality_rejected) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  auto make_cert = [&](std::uint8_t b) {
    std::vector<phantomx::consensus::Vote> votes;
    for (int i = 0; i < 3; ++i) {
      phantomx::consensus::Vote v;
      v.phase = phantomx::consensus::VotePhase::kPrecommit;
      v.epoch = 0; v.view = 0; v.height = 1; v.block_hash = h(b);
      v.validator_pubkey = keys[static_cast<std::size_t>(i)].pub.bytes; v.voting_power = 10;
      votes.push_back(phantomx::consensus::sign_vote(v, keys[static_cast<std::size_t>(i)].sec).value());
    }
    auto qc = phantomx::consensus::build_qc(vs, votes).value();
    phantomx::consensus::FinalityCertificate cert;
    cert.height = 1; cert.block_hash = h(b); cert.precommit_qc = qc;
    return cert;
  };
  PHX_EXPECT(engine.finalize(make_cert(1)).ok());
  PHX_EXPECT(!engine.finalize(make_cert(2)).ok()); // conflicting same-height finality
}

PHX_TEST(consensus_timeout_view_change_and_recovery) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  phantomx::consensus::BftEngine engine(make_vs(keys));
  PHX_EXPECT_EQ(static_cast<long long>(engine.state().view), 0LL);
  PHX_EXPECT(engine.timeout(100).ok());
  PHX_EXPECT_EQ(static_cast<long long>(engine.state().view), 1LL);
  // after restart/recovery, engine can accept votes in new view
  phantomx::consensus::Vote v;
  v.phase = phantomx::consensus::VotePhase::kPrevote;
  v.epoch = 0; v.view = 1; v.height = 1; v.block_hash = h(3);
  v.validator_pubkey = keys[0].pub.bytes; v.voting_power = 10;
  auto sv = phantomx::consensus::sign_vote(v, keys[0].sec);
  PHX_EXPECT(sv.ok());
  PHX_EXPECT(engine.submit_vote(sv.value()).ok());
}

PHX_TEST(consensus_offline_validator_no_quorum) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  std::vector<phantomx::consensus::Vote> votes; // only 2 votes, one offline/missing
  for (int i = 0; i < 2; ++i) {
    phantomx::consensus::Vote v;
    v.phase = phantomx::consensus::VotePhase::kPrecommit;
    v.epoch = 0; v.view = 0; v.height = 1; v.block_hash = h(9);
    v.validator_pubkey = keys[static_cast<std::size_t>(i)].pub.bytes; v.voting_power = 10;
    votes.push_back(phantomx::consensus::sign_vote(v, keys[static_cast<std::size_t>(i)].sec).value());
  }
  auto qc = phantomx::consensus::build_qc(vs, votes);
  PHX_EXPECT(!qc.ok()); // insufficient quorum
}

PHX_TEST(consensus_network_partition_no_finality) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  // partition: only 2 validators can communicate -> cannot form >2/3 quorum
  std::vector<phantomx::consensus::Vote> partition_votes;
  for (int i = 0; i < 2; ++i) {
    phantomx::consensus::Vote v;
    v.phase = phantomx::consensus::VotePhase::kPrecommit;
    v.epoch = 0; v.view = 0; v.height = 2; v.block_hash = h(7);
    v.validator_pubkey = keys[static_cast<std::size_t>(i)].pub.bytes; v.voting_power = 10;
    partition_votes.push_back(phantomx::consensus::sign_vote(v, keys[static_cast<std::size_t>(i)].sec).value());
  }
  auto qc = phantomx::consensus::build_qc(vs, partition_votes);
  PHX_EXPECT(!qc.ok());
}

PHX_TEST(consensus_delayed_messages_accepted_in_same_view) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  auto block = h(5);
  // delayed arrival: votes arrive later but still same epoch/view
  for (int i = 0; i < 3; ++i) {
    phantomx::consensus::Vote v;
    v.phase = phantomx::consensus::VotePhase::kPrevote;
    v.epoch = 0; v.view = 0; v.height = 1; v.block_hash = block;
    v.validator_pubkey = keys[static_cast<std::size_t>(i)].pub.bytes; v.voting_power = 10;
    auto sv = phantomx::consensus::sign_vote(v, keys[static_cast<std::size_t>(i)].sec);
    PHX_EXPECT(sv.ok());
    PHX_EXPECT(engine.submit_vote(sv.value()).ok());
  }
  auto qc = engine.maybe_qc(phantomx::consensus::VotePhase::kPrevote, 0, 0, block);
  PHX_EXPECT(qc.has_value());
}

PHX_TEST(consensus_validator_eligibility_caps_operator_power) {
  phantomx::consensus::VotingPowerPolicy policy;
  policy.min_poi_score = 50000;
  policy.max_operator_voting_power = 100000;
  std::vector<phantomx::consensus::ValidatorEligibilityInput> inputs;
  for (int i = 0; i < 3; ++i) {
    phantomx::consensus::ValidatorEligibilityInput in;
    in.validator_pubkey.fill(static_cast<std::uint8_t>(i + 1));
    in.operator_pubkey.fill(9); // same operator
    in.poi_score = 80000;
    in.identity_verified = true;
    inputs.push_back(in);
  }
  auto vs = phantomx::consensus::build_validator_set(1, inputs, policy);
  PHX_EXPECT(vs.ok());
  PHX_EXPECT_EQ(static_cast<long long>(vs.value().total_voting_power), 100000LL);
}

PHX_TEST(consensus_proposal_sign_verify_and_roundtrip) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  phantomx::consensus::BlockProposal p;
  p.epoch = 0; p.view = 0; p.height = 1;
  p.block_hash = h(7); p.parent_hash = h(6);
  p.proposer_pubkey = phantomx::consensus::select_proposer(vs, 0, 0);
  p.timestamp = 1234;
  int proposer_idx = -1;
  for (int i = 0; i < 4; ++i) if (keys[static_cast<std::size_t>(i)].pub.bytes == p.proposer_pubkey) proposer_idx = i;
  PHX_EXPECT(proposer_idx >= 0);
  auto sp = phantomx::consensus::sign_proposal(p, keys[static_cast<std::size_t>(proposer_idx)].sec);
  PHX_EXPECT(sp.ok());
  PHX_EXPECT(phantomx::consensus::verify_proposal(vs, sp.value()));
  auto bytes = phantomx::consensus::serialize_proposal(sp.value());
  auto decoded = phantomx::consensus::deserialize_proposal(bytes);
  PHX_EXPECT(decoded.ok());
  PHX_EXPECT(phantomx::consensus::verify_proposal(vs, decoded.value()));
  PHX_EXPECT(decoded.value().height == 1u);
  PHX_EXPECT(decoded.value().block_hash == h(7));
}

PHX_TEST(consensus_vote_wire_roundtrip_preserves_signature) {
  auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(kp.ok());
  phantomx::consensus::Vote v;
  v.phase = phantomx::consensus::VotePhase::kPrevote;
  v.epoch = 2; v.view = 3; v.height = 9; v.block_hash = h(9);
  v.validator_pubkey = kp.value().pub.bytes; v.voting_power = 10;
  auto sv = phantomx::consensus::sign_vote(v, kp.value().sec);
  PHX_EXPECT(sv.ok());
  auto wire = phantomx::network::encode_bft_vote(sv.value());
  auto dv = phantomx::network::decode_bft_vote(wire);
  PHX_EXPECT(dv.ok());
  PHX_EXPECT(dv.value().height == 9u);
  PHX_EXPECT(dv.value().block_hash == h(9));
  PHX_EXPECT(dv.value().signature == sv.value().signature);
}

PHX_TEST(consensus_qc_roundtrip_and_finality_wire_roundtrip) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  std::vector<phantomx::consensus::Vote> votes;
  for (int i = 0; i < 3; ++i) {
    phantomx::consensus::Vote v;
    v.phase = phantomx::consensus::VotePhase::kPrecommit;
    v.epoch = 0; v.view = 1; v.height = 5; v.block_hash = h(5);
    v.validator_pubkey = keys[static_cast<std::size_t>(i)].pub.bytes; v.voting_power = 10;
    votes.push_back(phantomx::consensus::sign_vote(v, keys[static_cast<std::size_t>(i)].sec).value());
  }
  auto qc = phantomx::consensus::build_qc(vs, votes);
  PHX_EXPECT(qc.ok());
  auto qc_bytes = phantomx::consensus::serialize_qc(qc.value());
  auto qc2 = phantomx::consensus::deserialize_qc(qc_bytes);
  PHX_EXPECT(qc2.ok());
  PHX_EXPECT(phantomx::consensus::verify_qc(vs, qc2.value()));

  phantomx::consensus::FinalityCertificate cert;
  cert.height = 5; cert.block_hash = h(5); cert.precommit_qc = qc.value();
  auto wire = phantomx::network::encode_bft_finality(cert);
  auto decoded = phantomx::network::decode_bft_finality(wire);
  PHX_EXPECT(decoded.ok());
  PHX_EXPECT(decoded.value().height == 5u);
  PHX_EXPECT(decoded.value().block_hash == h(5));
  PHX_EXPECT(phantomx::consensus::verify_qc(vs, decoded.value().precommit_qc));
}

PHX_TEST(consensus_finalize_idempotent_same_certificate) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  std::vector<phantomx::consensus::Vote> votes;
  for (int i = 0; i < 3; ++i) {
    phantomx::consensus::Vote v;
    v.phase = phantomx::consensus::VotePhase::kPrecommit;
    v.epoch = 0; v.view = 0; v.height = 1; v.block_hash = h(42);
    v.validator_pubkey = keys[static_cast<std::size_t>(i)].pub.bytes; v.voting_power = 10;
    votes.push_back(phantomx::consensus::sign_vote(v, keys[static_cast<std::size_t>(i)].sec).value());
  }
  auto qc = phantomx::consensus::build_qc(vs, votes);
  PHX_EXPECT(qc.ok());
  phantomx::consensus::FinalityCertificate cert;
  cert.height = 1; cert.block_hash = h(42); cert.precommit_qc = qc.value();
  PHX_EXPECT(engine.finalize(cert).ok());
  PHX_EXPECT(engine.finalize(cert).ok()); // idempotent
  PHX_EXPECT(engine.state().last_finalized.has_value());
  PHX_EXPECT_EQ(static_cast<long long>(engine.state().last_finalized->height), 1LL);
}

PHX_TEST(consensus_bft_wire_decode_rejects_malformed) {
  std::vector<std::uint8_t> bad = {1, 2, 3};
  PHX_EXPECT(!phantomx::network::decode_bft_vote(bad).ok());
  PHX_EXPECT(!phantomx::network::decode_bft_proposal(bad).ok());
  PHX_EXPECT(!phantomx::network::decode_bft_qc(bad).ok());
  PHX_EXPECT(!phantomx::network::decode_bft_finality(bad).ok());
  PHX_EXPECT(!phantomx::network::decode_bft_view_change(bad).ok());
}

PHX_TEST(consensus_bft_wal_save_load_roundtrip) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  phantomx::consensus::Vote v;
  v.phase = phantomx::consensus::VotePhase::kPrevote; v.epoch=0; v.view=0; v.height=1; v.block_hash=h(9);
  v.validator_pubkey=keys[0].pub.bytes; v.voting_power=10;
  auto sv = phantomx::consensus::sign_vote(v, keys[0].sec); PHX_EXPECT(sv.ok());
  PHX_EXPECT(engine.submit_vote(sv.value()).ok());
  std::vector<phantomx::consensus::Vote> pc;
  for (int i=0;i<3;++i){ phantomx::consensus::Vote x; x.phase=phantomx::consensus::VotePhase::kPrecommit; x.epoch=0; x.view=0; x.height=1; x.block_hash=h(9); x.validator_pubkey=keys[static_cast<size_t>(i)].pub.bytes; x.voting_power=10; pc.push_back(phantomx::consensus::sign_vote(x, keys[static_cast<size_t>(i)].sec).value()); }
  auto qc = phantomx::consensus::build_qc(vs, pc); PHX_EXPECT(qc.ok()); PHX_EXPECT(engine.observe_qc(qc.value()).ok());
  const std::string path = unique_wal_path("test");
  PHX_EXPECT(engine.save_state(path).ok());
  phantomx::consensus::BftEngine loaded(vs);
  PHX_EXPECT(loaded.load_state(path).ok());
  PHX_EXPECT_EQ(static_cast<long long>(loaded.state().view), 0LL);
  PHX_EXPECT(loaded.votes().size() == engine.votes().size());
  PHX_EXPECT(loaded.state().locked_hash.has_value());
  PHX_EXPECT(loaded.state().locked_hash.value() == h(9));
  PHX_EXPECT_EQ(static_cast<long long>(loaded.state().locked_height), 1LL);
}

PHX_TEST(consensus_bft_wal_corrupt_load_fails_gracefully) {
  const std::string path = unique_wal_path("corrupt");
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "garbage";
  }
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine loaded(vs);
  PHX_EXPECT(!loaded.load_state(path).ok());
}

PHX_TEST(consensus_bft_wal_restart_continues) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  std::vector<phantomx::consensus::Vote> pc;
  for (int i=0;i<3;++i){ phantomx::consensus::Vote x; x.phase=phantomx::consensus::VotePhase::kPrecommit; x.epoch=0; x.view=0; x.height=1; x.block_hash=h(9); x.validator_pubkey=keys[static_cast<size_t>(i)].pub.bytes; x.voting_power=10; pc.push_back(phantomx::consensus::sign_vote(x, keys[static_cast<size_t>(i)].sec).value()); }
  auto qc = phantomx::consensus::build_qc(vs, pc); PHX_EXPECT(qc.ok());
  PHX_EXPECT(engine.observe_qc(qc.value()).ok());
  const std::string path = unique_wal_path("restart");
  PHX_EXPECT(engine.save_state(path).ok());
  phantomx::consensus::BftEngine loaded(vs);
  PHX_EXPECT(loaded.load_state(path).ok());
  PHX_EXPECT(loaded.state().locked_hash.has_value());
  PHX_EXPECT(loaded.state().locked_hash.value() == h(9));
  // continue: can advance view
  PHX_EXPECT(loaded.enter_view(1).ok());
  PHX_EXPECT_EQ(static_cast<long long>(loaded.state().view), 1LL);
}

PHX_TEST(consensus_lock_rejects_different_hash_without_justify_qc) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  // lock on hash A via precommit QC
  std::vector<phantomx::consensus::Vote> votes;
  for (int i = 0; i < 3; ++i) {
    phantomx::consensus::Vote v; v.phase=phantomx::consensus::VotePhase::kPrecommit; v.epoch=0; v.view=0; v.height=1; v.block_hash=h(1);
    v.validator_pubkey=keys[static_cast<size_t>(i)].pub.bytes; v.voting_power=10;
    votes.push_back(phantomx::consensus::sign_vote(v, keys[static_cast<size_t>(i)].sec).value());
  }
  auto qc = phantomx::consensus::build_qc(vs, votes); PHX_EXPECT(qc.ok());
  PHX_EXPECT(engine.observe_qc(qc.value()).ok());
  PHX_EXPECT(engine.state().locked_hash.has_value());
  PHX_EXPECT(engine.state().locked_hash.value() == h(1));
  PHX_EXPECT_EQ(static_cast<long long>(engine.state().locked_height), 1LL);

  // A different hash at the locked height is unsafe without a newer QC.
  phantomx::consensus::BlockProposal conflict;
  conflict.epoch = 0; conflict.view = 1; conflict.height = 1;
  conflict.block_hash = h(2); conflict.parent_hash = h(0);
  PHX_EXPECT(!engine.check_proposal_lock(conflict).ok());

  // A direct child of the locked block is safe and needs no justify QC.
  phantomx::consensus::BlockProposal child;
  child.epoch = 0; child.view = 1; child.height = 2;
  child.block_hash = h(3); child.parent_hash = h(1);
  PHX_EXPECT(engine.check_proposal_lock(child).ok());

  // An orphan that does not extend the locked prefix is unsafe.
  phantomx::consensus::BlockProposal orphan;
  orphan.epoch = 0; orphan.view = 1; orphan.height = 2;
  orphan.block_hash = h(4); orphan.parent_hash = h(9);
  PHX_EXPECT(!engine.check_proposal_lock(orphan).ok());
}

PHX_TEST(consensus_lock_allows_height_zero_child) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  std::vector<phantomx::consensus::Vote> votes;
  for (int i = 0; i < 3; ++i) {
    phantomx::consensus::Vote v;
    v.phase = phantomx::consensus::VotePhase::kPrecommit; v.epoch = 0; v.view = 0; v.height = 0; v.block_hash = h(1);
    v.validator_pubkey = keys[static_cast<std::size_t>(i)].pub.bytes; v.voting_power = 10;
    votes.push_back(phantomx::consensus::sign_vote(v, keys[static_cast<std::size_t>(i)].sec).value());
  }
  auto qc = phantomx::consensus::build_qc(vs, votes);
  PHX_EXPECT(qc.ok());
  PHX_EXPECT(engine.observe_qc(qc.value()).ok());
  PHX_EXPECT_EQ(static_cast<long long>(engine.state().locked_height), 0LL);

  phantomx::consensus::BlockProposal child;
  child.epoch = 0; child.view = 1; child.height = 1;
  child.block_hash = h(2); child.parent_hash = h(1);
  PHX_EXPECT(engine.check_proposal_lock(child).ok());
}

PHX_TEST(consensus_view_change_roundtrip_and_quorum_advance) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  for (int i = 0; i < 3; ++i) {
    phantomx::consensus::ViewChange vc;
    vc.epoch = 0; vc.height = 0; vc.new_view = 1;
    vc.validator_pubkey = keys[static_cast<std::size_t>(i)].pub.bytes; vc.voting_power = 10;
    auto svc = phantomx::consensus::sign_view_change(vc, keys[static_cast<std::size_t>(i)].sec);
    PHX_EXPECT(svc.ok());
    auto wire = phantomx::network::encode_bft_view_change(svc.value());
    auto decoded = phantomx::network::decode_bft_view_change(wire);
    PHX_EXPECT(decoded.ok());
    PHX_EXPECT(phantomx::consensus::verify_view_change(vs, decoded.value()));
    PHX_EXPECT(engine.submit_view_change(decoded.value()).ok());
  }
  auto next = engine.maybe_advance_view_from_view_changes();
  PHX_EXPECT(next.has_value());
  PHX_EXPECT_EQ(static_cast<long long>(next.value()), 1LL);
  PHX_EXPECT(engine.enter_view(next.value()).ok());
  PHX_EXPECT_EQ(static_cast<long long>(engine.state().view), 1LL);
}

PHX_TEST(consensus_view_change_rejects_height_mismatch) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  phantomx::consensus::ViewChange vc;
  vc.epoch = 0; vc.height = 1; vc.new_view = 1;
  vc.validator_pubkey = keys[0].pub.bytes; vc.voting_power = 10;
  auto signed_vc = phantomx::consensus::sign_view_change(vc, keys[0].sec);
  PHX_EXPECT(signed_vc.ok());
  // A view change bound to another height must never advance this height.
  PHX_EXPECT(!engine.submit_view_change(signed_vc.value()).ok());
  PHX_EXPECT(!engine.maybe_view_change_qc().has_value());
}

PHX_TEST(consensus_lock_accepts_higher_view_justify_qc) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  const auto make_qc = [&](phantomx::consensus::VotePhase phase, std::uint64_t view,
                           const phantomx::Hash256Bytes& hash) {
    std::vector<phantomx::consensus::Vote> votes;
    for (int i = 0; i < 3; ++i) {
      phantomx::consensus::Vote v;
      v.phase = phase; v.epoch = 0; v.view = view; v.height = 1; v.block_hash = hash;
      v.validator_pubkey = keys[static_cast<std::size_t>(i)].pub.bytes; v.voting_power = 10;
      votes.push_back(phantomx::consensus::sign_vote(v, keys[static_cast<std::size_t>(i)].sec).value());
    }
    return phantomx::consensus::build_qc(vs, votes).value();
  };
  PHX_EXPECT(engine.observe_qc(make_qc(phantomx::consensus::VotePhase::kPrecommit, 0, h(1))).ok());
  PHX_EXPECT(engine.state().locked_view == 0);

  phantomx::consensus::BlockProposal proposal;
  proposal.epoch = 0; proposal.view = 1; proposal.height = 1; proposal.block_hash = h(2);
  PHX_EXPECT(!engine.check_proposal_lock(proposal).ok()); // locked, no justify

  proposal.justify_qc = make_qc(phantomx::consensus::VotePhase::kPrevote, 1, h(2));
  PHX_EXPECT(engine.check_proposal_lock(proposal).ok()); // higher view unlocks

  proposal.justify_qc = make_qc(phantomx::consensus::VotePhase::kPrevote, 0, h(2));
  PHX_EXPECT(!engine.check_proposal_lock(proposal).ok()); // same view is not enough
}

PHX_TEST(consensus_new_view_roundtrip_and_verify) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  for (int i = 0; i < 3; ++i) {
    phantomx::consensus::ViewChange vc;
    vc.epoch = 0; vc.height = 0; vc.new_view = 1;
    vc.validator_pubkey = keys[static_cast<std::size_t>(i)].pub.bytes; vc.voting_power = 10;
    PHX_EXPECT(engine.submit_view_change(
        phantomx::consensus::sign_view_change(vc, keys[static_cast<std::size_t>(i)].sec).value()).ok());
  }
  auto vc_qc = engine.maybe_view_change_qc();
  PHX_EXPECT(vc_qc.has_value());
  const auto proposer = phantomx::consensus::select_new_view_proposer(vs, vc_qc.value());
  const phantomx::crypto::KeyPair* proposer_key = nullptr;
  for (const auto& k : keys) if (k.pub.bytes == proposer) proposer_key = &k;
  PHX_EXPECT(proposer_key != nullptr);

  phantomx::consensus::NewView nv;
  nv.epoch = 0; nv.height = 0; nv.view = 1;
  nv.view_change_qc = vc_qc.value();
  nv.proposer_pubkey = proposer;
  auto signed_nv = phantomx::consensus::sign_new_view(nv, proposer_key->sec);
  PHX_EXPECT(signed_nv.ok());
  auto wire = phantomx::network::encode_bft_new_view(signed_nv.value());
  auto decoded = phantomx::network::decode_bft_new_view(wire);
  PHX_EXPECT(decoded.ok());
  PHX_EXPECT(phantomx::consensus::verify_new_view(vs, decoded.value()));

  auto tampered = decoded.value();
  tampered.view = 2;
  PHX_EXPECT(!phantomx::consensus::verify_new_view(vs, tampered));
}

PHX_TEST(consensus_new_view_fallback_when_nominal_proposer_is_down) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  const auto nominal = phantomx::consensus::select_proposer(vs, 0, 1);
  std::size_t down = keys.size();
  for (std::size_t i = 0; i < keys.size(); ++i) {
    if (keys[i].pub.bytes == nominal) { down = i; break; }
  }
  PHX_EXPECT(down < keys.size());

  phantomx::consensus::BftEngine engine(vs);
  for (std::size_t i = 0; i < keys.size(); ++i) {
    if (i == down) continue;
    phantomx::consensus::ViewChange vc;
    vc.epoch = 0; vc.height = 0; vc.new_view = 1;
    vc.validator_pubkey = keys[i].pub.bytes; vc.voting_power = 10;
    PHX_EXPECT(engine.submit_view_change(
        phantomx::consensus::sign_view_change(vc, keys[i].sec).value()).ok());
  }
  auto qc = engine.maybe_view_change_qc();
  PHX_EXPECT(qc.has_value());
  const auto fallback = phantomx::consensus::select_new_view_proposer(vs, qc.value());
  PHX_EXPECT(fallback != nominal);

  const phantomx::crypto::KeyPair* fallback_key = nullptr;
  for (const auto& k : keys) if (k.pub.bytes == fallback) fallback_key = &k;
  PHX_EXPECT(fallback_key != nullptr);

  phantomx::consensus::NewView nv;
  nv.epoch = 0; nv.height = 0; nv.view = 1;
  nv.view_change_qc = qc.value();
  nv.proposer_pubkey = fallback;
  auto signed_nv = phantomx::consensus::sign_new_view(nv, fallback_key->sec);
  PHX_EXPECT(signed_nv.ok());
  PHX_EXPECT(phantomx::consensus::verify_new_view(vs, signed_nv.value()));
}

PHX_TEST(consensus_view_change_dedups_resend_and_detects_equivocation) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);

  phantomx::consensus::ViewChange first;
  first.epoch = 0; first.height = 0; first.new_view = 1;
  first.validator_pubkey = keys[0].pub.bytes; first.voting_power = 10;
  auto signed_first = phantomx::consensus::sign_view_change(first, keys[0].sec);
  PHX_EXPECT(signed_first.ok());
  PHX_EXPECT(engine.submit_view_change(signed_first.value()).ok());
  // Byte-identical retransmission is benign.
  PHX_EXPECT(engine.submit_view_change(signed_first.value()).ok());
  PHX_EXPECT_EQ(static_cast<unsigned long long>(engine.view_change_equivocations()), 0ULL);

  // Same validator/slot with a different high QC is equivocation.
  std::vector<phantomx::consensus::Vote> votes;
  for (int i = 0; i < 3; ++i) {
    phantomx::consensus::Vote v;
    v.phase = phantomx::consensus::VotePhase::kPrevote; v.epoch = 0; v.view = 0; v.height = 0; v.block_hash = h(1);
    v.validator_pubkey = keys[static_cast<std::size_t>(i)].pub.bytes; v.voting_power = 10;
    votes.push_back(phantomx::consensus::sign_vote(v, keys[static_cast<std::size_t>(i)].sec).value());
  }
  auto high_qc = phantomx::consensus::build_qc(vs, votes);
  PHX_EXPECT(high_qc.ok());
  auto conflicting = signed_first.value();
  conflicting.high_qc = high_qc.value();
  auto signed_conflict = phantomx::consensus::sign_view_change(conflicting, keys[0].sec);
  PHX_EXPECT(signed_conflict.ok());
  PHX_EXPECT(engine.submit_view_change(signed_conflict.value()).ok());
  PHX_EXPECT_EQ(static_cast<unsigned long long>(engine.view_change_equivocations()), 1ULL);
}


PHX_TEST(consensus_slashed_validator_rejected_and_persisted) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) {
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok());
    keys.push_back(kp.value());
  }
  phantomx::consensus::BftEngine engine(make_vs(keys));
  phantomx::consensus::Vote v;
  v.phase = phantomx::consensus::VotePhase::kPrevote;
  v.epoch = 0; v.view = 0; v.height = 1; v.block_hash = h(1);
  v.validator_pubkey = keys[0].pub.bytes; v.voting_power = 10;
  auto s1 = phantomx::consensus::sign_vote(v, keys[0].sec);
  PHX_EXPECT(s1.ok());
  PHX_EXPECT(engine.submit_vote(s1.value()).ok());
  auto v2 = v; v2.block_hash = h(2);
  auto s2 = phantomx::consensus::sign_vote(v2, keys[0].sec);
  PHX_EXPECT(s2.ok());
  PHX_EXPECT(!engine.submit_vote(s2.value()).ok());
  PHX_EXPECT(engine.is_slashed(keys[0].pub.bytes));

  // Slashed validator cannot submit new votes.
  phantomx::consensus::Vote v3;
  v3.phase = phantomx::consensus::VotePhase::kPrevote;
  v3.epoch = 0; v3.view = 0; v3.height = 1; v3.block_hash = h(3);
  v3.validator_pubkey = keys[0].pub.bytes; v3.voting_power = 10;
  auto s3 = phantomx::consensus::sign_vote(v3, keys[0].sec);
  PHX_EXPECT(s3.ok());
  PHX_EXPECT(!engine.submit_vote(s3.value()).ok());

  // Slashing persists across WAL save/load.
  auto path = unique_wal_path("slash");
  PHX_EXPECT(engine.save_state(path).ok());
  phantomx::consensus::BftEngine loaded(make_vs(keys));
  PHX_EXPECT(loaded.load_state(path).ok());
  PHX_EXPECT(loaded.is_slashed(keys[0].pub.bytes));
  PHX_EXPECT_EQ(static_cast<long long>(loaded.equivocations().size()), 1LL);
  std::error_code ec; std::filesystem::remove(path, ec);
}

PHX_TEST(consensus_pacemaker_backoff_and_progress) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  phantomx::consensus::BftEngine engine(make_vs(keys));
  phantomx::consensus::PacemakerConfig cfg; cfg.base_timeout_ms = 100; cfg.max_timeout_ms = 500; cfg.backoff_numerator = 2; cfg.backoff_denominator = 1;
  engine.set_pacemaker_config(cfg);
  engine.note_progress(1000);
  PHX_EXPECT(!engine.should_timeout(1050));
  PHX_EXPECT(engine.should_timeout(1101));
  PHX_EXPECT_EQ(static_cast<long long>(engine.pacemaker_timeout_ms()), 100LL);
  PHX_EXPECT(engine.timeout(1101).ok());
  PHX_EXPECT_EQ(static_cast<long long>(engine.state().view), 1LL);
  PHX_EXPECT_EQ(static_cast<long long>(engine.pacemaker_timeout_ms()), 200LL);
  PHX_EXPECT(engine.timeout(1401).ok());
  PHX_EXPECT_EQ(static_cast<long long>(engine.pacemaker_timeout_ms()), 400LL);
  PHX_EXPECT(engine.timeout(1901).ok());
  PHX_EXPECT_EQ(static_cast<long long>(engine.pacemaker_timeout_ms()), 500LL); // capped
  engine.note_progress(2000);
  PHX_EXPECT_EQ(static_cast<long long>(engine.pacemaker_timeout_ms()), 100LL);
  PHX_EXPECT_EQ(static_cast<long long>(engine.pacemaker().timeouts_in_view), 0LL);
}

PHX_TEST(consensus_commit_reveal_beacon_deterministic) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  phantomx::consensus::BftEngine engine(make_vs(keys));
  auto make_commit = [&](int i, const phantomx::Hash256Bytes& reveal) {
    phantomx::consensus::RandomnessCommit c;
    c.epoch = 0; c.validator_pubkey = keys[i].pub.bytes;
    // commitment = sha256d(dom||reveal||pubkey||epoch)
    phantomx::BinaryWriter w; const std::string dom="PHX/rand/commit/v1";
    w.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(dom.data()), dom.size()));
    w.write_bytes(std::span<const std::uint8_t>(reveal.data(), reveal.size()));
    w.write_bytes(std::span<const std::uint8_t>(c.validator_pubkey.data(), c.validator_pubkey.size()));
    w.write_u64(c.epoch);
    auto bytes=w.take();
    c.commitment = phantomx::sha256d(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
    // sign commitment
    phantomx::BinaryWriter sw; const std::string sdom="PHX/rand/commit-sig/v1";
    sw.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(sdom.data()), sdom.size()));
    sw.write_u64(c.epoch);
    sw.write_bytes(std::span<const std::uint8_t>(c.validator_pubkey.data(), c.validator_pubkey.size()));
    sw.write_bytes(std::span<const std::uint8_t>(c.commitment.data(), c.commitment.size()));
    auto sb=sw.take();
    auto sig=phantomx::crypto::sign(std::span<const std::uint8_t>(sb.data(),sb.size()), keys[i].sec); PHX_EXPECT(sig.ok());
    c.signature=sig.value().bytes; return c;
  };
  auto make_reveal = [&](int i, const phantomx::Hash256Bytes& reveal) {
    phantomx::consensus::RandomnessReveal r; r.epoch=0; r.validator_pubkey=keys[i].pub.bytes; r.reveal=reveal;
    phantomx::BinaryWriter sw; const std::string sdom="PHX/rand/reveal-sig/v1";
    sw.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(sdom.data()), sdom.size()));
    sw.write_u64(r.epoch);
    sw.write_bytes(std::span<const std::uint8_t>(r.validator_pubkey.data(), r.validator_pubkey.size()));
    sw.write_bytes(std::span<const std::uint8_t>(r.reveal.data(), r.reveal.size()));
    auto sb=sw.take(); auto sig=phantomx::crypto::sign(std::span<const std::uint8_t>(sb.data(),sb.size()), keys[i].sec); PHX_EXPECT(sig.ok()); r.signature=sig.value().bytes; return r;
  };
  auto r1=h(11), r2=h(22), r3=h(33);
  PHX_EXPECT(engine.submit_randomness_commit(make_commit(0,r1)).ok());
  PHX_EXPECT(engine.submit_randomness_commit(make_commit(1,r2)).ok());
  PHX_EXPECT(engine.submit_randomness_commit(make_commit(2,r3)).ok());
  // wrong reveal rejected
  PHX_EXPECT(!engine.submit_randomness_reveal(make_reveal(0,r2)).ok());
  PHX_EXPECT(engine.submit_randomness_reveal(make_reveal(0,r1)).ok());
  PHX_EXPECT(engine.submit_randomness_reveal(make_reveal(1,r2)).ok());
  PHX_EXPECT(engine.submit_randomness_reveal(make_reveal(2,r3)).ok());
  auto b1 = engine.randomness_beacon(0);
  auto b2 = engine.randomness_beacon(0);
  PHX_EXPECT(b1.has_value()); PHX_EXPECT(b2.has_value());
  PHX_EXPECT(b1.value() == b2.value());
}

PHX_TEST(consensus_slashing_governance_appeal_flow) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  phantomx::consensus::BftEngine engine(make_vs(keys));
  auto pk = keys[0].pub.bytes;
  PHX_EXPECT(engine.record_slashing(pk, phantomx::consensus::SlashingReason::kVoteEquivocation, 0, 0, 1, 1000, "ev1").ok());
  PHX_EXPECT(engine.is_slashed(pk));
  PHX_EXPECT_EQ(static_cast<long long>(engine.slashing_records().size()), 1LL);
  PHX_EXPECT(engine.appeal_slashing(0, "proof").ok());
  PHX_EXPECT(engine.slashing_records()[0].appealed);
  PHX_EXPECT(engine.resolve_appeal(0, true).ok());
  PHX_EXPECT(!engine.is_slashed(pk));
  PHX_EXPECT(engine.slashing_records()[0].appeal_resolved);
  PHX_EXPECT(engine.slashing_records()[0].appeal_accepted);
}

PHX_TEST(consensus_adversarial_safety_no_conflicting_finality) {
  // 4 validators, 1 Byzantine attempts conflicting precommit QC; engine must reject conflicting finality.
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  auto make_cert = [&](std::uint8_t bh) {
    std::vector<phantomx::consensus::Vote> votes;
    for (int i=0;i<3;++i){ phantomx::consensus::Vote v; v.phase=phantomx::consensus::VotePhase::kPrecommit; v.epoch=0; v.view=0; v.height=1; v.block_hash=h(bh); v.validator_pubkey=keys[i].pub.bytes; v.voting_power=10; votes.push_back(phantomx::consensus::sign_vote(v,keys[i].sec).value()); }
    auto qc = phantomx::consensus::build_qc(vs, votes).value();
    phantomx::consensus::FinalityCertificate c; c.height=1; c.block_hash=h(bh); c.precommit_qc=qc; return c;
  };
  PHX_EXPECT(engine.finalize(make_cert(1)).ok());
  PHX_EXPECT(!engine.finalize(make_cert(2)).ok());
  PHX_EXPECT(engine.state().last_finalized.has_value());
  PHX_EXPECT(engine.state().last_finalized->block_hash == h(1));
}

PHX_TEST(consensus_adversarial_partition_no_quorum) {
  // Network partition: only 2 of 4 validators visible, quorum impossible.
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto vs = make_vs(keys);
  std::vector<phantomx::consensus::Vote> votes;
  for (int i=0;i<2;++i){ phantomx::consensus::Vote v; v.phase=phantomx::consensus::VotePhase::kPrecommit; v.epoch=0; v.view=0; v.height=5; v.block_hash=h(7); v.validator_pubkey=keys[i].pub.bytes; v.voting_power=10; votes.push_back(phantomx::consensus::sign_vote(v,keys[i].sec).value()); }
  auto qc = phantomx::consensus::build_qc(vs, votes);
  PHX_EXPECT(!qc.ok());
}

PHX_TEST(consensus_liveness_simulation_view_change_recovers) {
  // Simulate stalled view: timeouts advance view until quorum view-change possible, then NewView allows progress.
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto vs = make_vs(keys);
  phantomx::consensus::BftEngine engine(vs);
  phantomx::consensus::PacemakerConfig cfg; cfg.base_timeout_ms=100; cfg.max_timeout_ms=1000; cfg.backoff_numerator=2; cfg.backoff_denominator=1;
  engine.set_pacemaker_config(cfg);
  engine.note_progress(0);
  // timeout fires, view advances
  PHX_EXPECT(engine.should_timeout(101));
  PHX_EXPECT(engine.timeout(101).ok());
  PHX_EXPECT_EQ(static_cast<long long>(engine.state().view), 1LL);
  // 3 validators submit view changes for view 2
  for (int i=0;i<3;++i) {
    phantomx::consensus::ViewChange vc; vc.epoch=0; vc.height=0; vc.new_view=2; vc.validator_pubkey=keys[i].pub.bytes; vc.voting_power=10;
    auto svc = phantomx::consensus::sign_view_change(vc, keys[i].sec); PHX_EXPECT(svc.ok());
    PHX_EXPECT(engine.submit_view_change(svc.value()).ok());
  }
  auto qc = engine.maybe_view_change_qc();
  PHX_EXPECT(qc.has_value());
  PHX_EXPECT_EQ(static_cast<long long>(qc->new_view), 2LL);
}

PHX_TEST(consensus_randomness_reveal_deadline_slashes_withholder) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  phantomx::consensus::BftEngine engine(make_vs(keys));
  engine.set_randomness_reveal_deadline_blocks(2);
  phantomx::Hash256Bytes reveal = h(42);
  // build commit
  phantomx::consensus::RandomnessCommit c; c.epoch=0; c.height=1; c.validator_pubkey=keys[0].pub.bytes;
  phantomx::BinaryWriter w; const std::string dom="PHX/rand/commit/v1";
  w.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(dom.data()), dom.size()));
  w.write_bytes(std::span<const std::uint8_t>(reveal.data(), reveal.size()));
  w.write_bytes(std::span<const std::uint8_t>(c.validator_pubkey.data(), c.validator_pubkey.size()));
  w.write_u64(c.epoch);
  auto bytes=w.take(); c.commitment=phantomx::sha256d(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
  phantomx::BinaryWriter sw; const std::string sdom="PHX/rand/commit-sig/v1";
  sw.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(sdom.data()), sdom.size()));
  sw.write_u64(c.epoch); sw.write_bytes(std::span<const std::uint8_t>(c.validator_pubkey.data(), c.validator_pubkey.size())); sw.write_bytes(std::span<const std::uint8_t>(c.commitment.data(), c.commitment.size()));
  auto sb=sw.take(); auto sig=phantomx::crypto::sign(std::span<const std::uint8_t>(sb.data(),sb.size()), keys[0].sec); PHX_EXPECT(sig.ok()); c.signature=sig.value().bytes;
  PHX_EXPECT(engine.submit_randomness_commit(c).ok());
  PHX_EXPECT(!engine.is_slashed(keys[0].pub.bytes));
  PHX_EXPECT(engine.enforce_randomness_deadline(3).ok()); // height 1 + 2 <= 3
  PHX_EXPECT(engine.is_slashed(keys[0].pub.bytes));
  PHX_EXPECT(engine.slashing_records().size() >= 1);
}

PHX_TEST(consensus_slashing_settlement_economics) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  phantomx::consensus::BftEngine engine(make_vs(keys));
  auto pk = keys[0].pub.bytes;
  PHX_EXPECT(engine.record_slashing(pk, phantomx::consensus::SlashingReason::kVoteEquivocation, 0, 0, 1, 500, "ev").ok());
  PHX_EXPECT_EQ(static_cast<long long>(engine.total_slashed_penalty_atoms()), 500LL);
  PHX_EXPECT_EQ(static_cast<long long>(engine.unsettled_slashed_penalty_atoms()), 500LL);
  PHX_EXPECT(engine.settle_slashing(0, "settlement://tx1").ok());
  PHX_EXPECT_EQ(static_cast<long long>(engine.unsettled_slashed_penalty_atoms()), 0LL);
  PHX_EXPECT(engine.slashing_records()[0].settled);
}

// --- PB-001 BFT config / operator key tests ---

namespace {
phantomx::Config pb001_make_config(const std::map<std::string, std::string>& kv) {
  phantomx::Config cfg;
  for (const auto& [k, v] : kv) cfg.set(k, v);
  return cfg;
}

std::string pb001_validators_json(const std::vector<phantomx::crypto::KeyPair>& keys) {
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < keys.size(); ++i) {
    if (i) oss << ",";
    oss << "{\"pubkey\":\"" << phantomx::crypto::serialize_public_key_hex(keys[i].pub) << "\",\"voting_power\":1}";
  }
  oss << "]";
  return oss.str();
}

std::filesystem::path pb001_write_key_file(const phantomx::crypto::KeyPair& kp, const std::string& tag) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto p = std::filesystem::temp_directory_path() / ("phantomx-pb001-" + tag + "-" + std::to_string(stamp) + ".key");
  std::ofstream out(p, std::ios::trunc);
  out << phantomx::crypto::serialize_secret_key_hex(kp.sec);
  out.close();
  return p;
}
}  // namespace

PHX_TEST(pb001_bft_config_valid_operator_mode_four_validators) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto cfg = pb001_make_config({
    {"bft.enabled","true"},
    {"bft.validator_count","4"},
    {"bft.view_change_enabled","true"},
    {"bft.key_mode","operator"},
    {"bft.validators", pb001_validators_json(keys)},
  });
  auto parsed = phantomx::consensus::parse_bft_config(cfg);
  PHX_EXPECT(parsed.ok());
  PHX_EXPECT(parsed.value().enabled);
  PHX_EXPECT_EQ(static_cast<long long>(parsed.value().validator_count), 4LL);
  PHX_EXPECT(parsed.value().key_mode == phantomx::consensus::BftKeyMode::kOperator);
  PHX_EXPECT_EQ(static_cast<long long>(parsed.value().validators.size()), 4LL);
  auto vs = phantomx::consensus::build_validator_set(parsed.value(), 0);
  PHX_EXPECT(vs.ok());
  PHX_EXPECT_EQ(static_cast<long long>(vs.value().validators.size()), 4LL);
  PHX_EXPECT_EQ(static_cast<long long>(vs.value().total_voting_power), 4LL);
}

PHX_TEST(pb001_bft_config_rejects_missing_bft_when_enabled_by_consensus) {
  auto cfg = pb001_make_config({{"consensus","bft"}});
  auto parsed = phantomx::consensus::parse_bft_config(cfg);
  PHX_EXPECT(!parsed.ok());
}

PHX_TEST(pb001_bft_config_rejects_invalid_validator_count) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 3; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto cfg = pb001_make_config({
    {"bft.enabled","true"},{"bft.validator_count","3"},{"bft.key_mode","operator"},{"bft.validators", pb001_validators_json(keys)}
  });
  auto parsed = phantomx::consensus::parse_bft_config(cfg);
  PHX_EXPECT(!parsed.ok());
}

PHX_TEST(pb001_bft_config_rejects_duplicate_validator_identities) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  keys[3] = keys[0]; // duplicate pubkey
  auto cfg = pb001_make_config({
    {"bft.enabled","true"},{"bft.validator_count","4"},{"bft.key_mode","operator"},{"bft.validators", pb001_validators_json(keys)}
  });
  auto parsed = phantomx::consensus::parse_bft_config(cfg);
  PHX_EXPECT(!parsed.ok());
}

PHX_TEST(pb001_bft_config_rejects_invalid_or_missing_pubkey) {
  auto cfg = pb001_make_config({
    {"bft.enabled","true"},{"bft.validator_count","4"},{"bft.key_mode","operator"},
    {"bft.validators","[{\"pubkey\":\"zzzz\",\"voting_power\":1},{\"pubkey\":\"aa\",\"voting_power\":1},{\"pubkey\":\"bb\",\"voting_power\":1},{\"pubkey\":\"cc\",\"voting_power\":1}]"}
  });
  auto parsed = phantomx::consensus::parse_bft_config(cfg);
  PHX_EXPECT(!parsed.ok());
}

PHX_TEST(pb001_bft_config_rejects_seed_prefix_in_operator_mode) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto cfg = pb001_make_config({
    {"bft.enabled","true"},{"bft.validator_count","4"},{"bft.key_mode","operator"},
    {"bft.validator_seed_prefix","phantomx-regtest-validator-"},
    {"bft.validators", pb001_validators_json(keys)}
  });
  auto parsed = phantomx::consensus::parse_bft_config(cfg);
  PHX_EXPECT(!parsed.ok());
}

PHX_TEST(pb001_bft_config_seed_mode_preserves_regtest_shape) {
  auto cfg = pb001_make_config({
    {"bft.enabled","true"},{"bft.validator_count","4"},{"bft.view_change_enabled","true"},
    {"bft.validator_seed_prefix","phantomx-regtest-validator-"}
  });
  auto parsed = phantomx::consensus::parse_bft_config(cfg);
  PHX_EXPECT(parsed.ok());
  PHX_EXPECT(parsed.value().key_mode == phantomx::consensus::BftKeyMode::kSeed);
  auto vs = phantomx::consensus::build_validator_set(parsed.value(), 0);
  PHX_EXPECT(vs.ok());
  PHX_EXPECT_EQ(static_cast<long long>(vs.value().validators.size()), 4LL);
}

PHX_TEST(pb001_operator_key_load_valid_matching_identity) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto cfg = pb001_make_config({
    {"bft.enabled","true"},{"bft.validator_count","4"},{"bft.key_mode","operator"},{"bft.validators", pb001_validators_json(keys)}
  });
  auto parsed = phantomx::consensus::parse_bft_config(cfg);
  PHX_EXPECT(parsed.ok());
  auto kf = pb001_write_key_file(keys[2], "valid");
  auto id = phantomx::consensus::load_operator_identity(parsed.value(), kf);
  PHX_EXPECT(id.ok());
  PHX_EXPECT_EQ(static_cast<long long>(id.value().validator_index), 2LL);
  PHX_EXPECT(id.value().validator_pubkey == keys[2].pub.bytes);
  std::filesystem::remove(kf);
}

PHX_TEST(pb001_operator_key_load_rejects_missing_invalid_mismatch) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto cfg = pb001_make_config({
    {"bft.enabled","true"},{"bft.validator_count","4"},{"bft.key_mode","operator"},{"bft.validators", pb001_validators_json(keys)}
  });
  auto parsed = phantomx::consensus::parse_bft_config(cfg);
  PHX_EXPECT(parsed.ok());

  auto missing = phantomx::consensus::load_operator_identity(parsed.value(), "/tmp/does-not-exist-pb001.key");
  PHX_EXPECT(!missing.ok());

  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  auto bad = std::filesystem::temp_directory_path() / ("phantomx-pb001-bad-" + std::to_string(stamp) + ".key");
  { std::ofstream out(bad); out << "not-a-key"; }
  auto invalid = phantomx::consensus::load_operator_identity(parsed.value(), bad);
  PHX_EXPECT(!invalid.ok());
  std::filesystem::remove(bad);

  auto outsider = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(outsider.ok());
  auto kf = pb001_write_key_file(outsider.value(), "mismatch");
  auto mismatch = phantomx::consensus::load_operator_identity(parsed.value(), kf);
  PHX_EXPECT(!mismatch.ok());
  std::filesystem::remove(kf);
}

PHX_TEST(pb001_operator_key_file_error_does_not_leak_secret) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto cfg = pb001_make_config({
    {"bft.enabled","true"},{"bft.validator_count","4"},{"bft.key_mode","operator"},{"bft.validators", pb001_validators_json(keys)}
  });
  auto parsed = phantomx::consensus::parse_bft_config(cfg);
  PHX_EXPECT(parsed.ok());
  auto outsider = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(outsider.ok());
  auto kf = pb001_write_key_file(outsider.value(), "nosecret");
  auto mismatch = phantomx::consensus::load_operator_identity(parsed.value(), kf);
  PHX_EXPECT(!mismatch.ok());
  const auto sec_hex = phantomx::crypto::serialize_secret_key_hex(outsider.value().sec);
  PHX_EXPECT(mismatch.error().to_string().find(sec_hex) == std::string::npos);
  std::filesystem::remove(kf);
}

PHX_TEST(pb001_bft_config_rejects_contradictory_or_missing_operator_fields) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  // missing validators
  auto missing = pb001_make_config({{"bft.enabled","true"},{"bft.validator_count","4"},{"bft.key_mode","operator"}});
  PHX_EXPECT(!phantomx::consensus::parse_bft_config(missing).ok());
  // count mismatch
  auto mismatch = pb001_make_config({{"bft.enabled","true"},{"bft.validator_count","4"},{"bft.key_mode","operator"},{"bft.validators", pb001_validators_json(std::vector<phantomx::crypto::KeyPair>(keys.begin(), keys.begin()+3))}});
  PHX_EXPECT(!phantomx::consensus::parse_bft_config(mismatch).ok());
  // unsupported key mode
  auto badmode = pb001_make_config({{"bft.enabled","true"},{"bft.validator_count","4"},{"bft.key_mode","magic"},{"bft.validators", pb001_validators_json(keys)}});
  PHX_EXPECT(!phantomx::consensus::parse_bft_config(badmode).ok());
}

PHX_TEST(pb001_operator_key_rotation_and_backup) {
  // Generate A, config with A, verify load OK
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i = 0; i < 4; ++i) { auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  auto cfgA = pb001_make_config({{"bft.enabled","true"},{"bft.validator_count","4"},{"bft.key_mode","operator"},{"bft.validators", pb001_validators_json(keys)}});
  auto parsedA = phantomx::consensus::parse_bft_config(cfgA);
  PHX_EXPECT(parsedA.ok());
  auto kfA = pb001_write_key_file(keys[1], "rotA");
  auto idA = phantomx::consensus::load_operator_identity(parsedA.value(), kfA);
  PHX_EXPECT(idA.ok());
  PHX_EXPECT_EQ(static_cast<long long>(idA.value().validator_index), 1LL);

  // Backup: copy key file, ensure load still works
  auto backup = kfA;
  backup += ".bak";
  std::filesystem::copy_file(kfA, backup, std::filesystem::copy_options::overwrite_existing);
  auto idBak = phantomx::consensus::load_operator_identity(parsedA.value(), backup);
  PHX_EXPECT(idBak.ok());

  // Rotation: replace validator 1 with new key B in config
  auto newKp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(newKp.ok());
  keys[1] = newKp.value();
  auto cfgB = pb001_make_config({{"bft.enabled","true"},{"bft.validator_count","4"},{"bft.key_mode","operator"},{"bft.validators", pb001_validators_json(keys)}});
  auto parsedB = phantomx::consensus::parse_bft_config(cfgB);
  PHX_EXPECT(parsedB.ok());
  auto kfB = pb001_write_key_file(newKp.value(), "rotB");
  auto idB = phantomx::consensus::load_operator_identity(parsedB.value(), kfB);
  PHX_EXPECT(idB.ok());
  PHX_EXPECT_EQ(static_cast<long long>(idB.value().validator_index), 1LL);
  // Old key A must not match new config
  auto oldVsNew = phantomx::consensus::load_operator_identity(parsedB.value(), kfA);
  PHX_EXPECT(!oldVsNew.ok());

  std::filesystem::remove(kfA);
  std::filesystem::remove(kfB);
  std::filesystem::remove(backup);
}
