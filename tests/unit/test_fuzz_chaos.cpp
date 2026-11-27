#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "phantomx/common/serialization.hpp"
#include "phantomx/aggregator/aggregator.hpp"
#include "phantomx/common/crypto.hpp"

namespace {
phantomx::Hash256Bytes h(std::uint8_t b){ phantomx::Hash256Bytes o{}; o[0]=b; return o; }
std::array<std::uint8_t,32> id(std::uint8_t b){ std::array<std::uint8_t,32> o{}; o[0]=b; return o; }
}

PHX_TEST(fuzz_serialization_u64_roundtrip) {
  std::mt19937_64 rng(0xF00D);
  for (int i=0;i<500;++i) {
    std::uint64_t v = (i<50) ? static_cast<std::uint64_t>(i) : rng();
    phantomx::BinaryWriter w; w.write_u64(v);
    phantomx::BinaryReader r(w.buffer());
    auto out = r.read_u64();
    PHX_EXPECT(out.ok());
    PHX_EXPECT(out.value() == v);
  }
}

PHX_TEST(fuzz_serialization_bytes_roundtrip) {
  std::mt19937 rng(123);
  for (int i=0;i<200;++i) {
    std::vector<std::uint8_t> buf((rng()%256));
    for (auto& b: buf) b = static_cast<std::uint8_t>(rng()&0xff);
    phantomx::BinaryWriter w; w.write_bytes(std::span<const std::uint8_t>(buf.data(), buf.size()));
    phantomx::BinaryReader r(w.buffer());
    auto out = r.read_bytes(buf.size());
    PHX_EXPECT(out.ok());
    PHX_EXPECT(out.value().size() == buf.size());
  }
}

PHX_TEST(fuzz_aggregator_invalid_signatures_rejected) {
  std::mt19937 rng(7);
  auto ag = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(ag.ok());
  phantomx::aggregator::AggregationRequest req;
  req.network_id="regtest"; req.assignment_id=id(1); req.subject_id=id(2); req.evidence_hash=h(3); req.quorum.required=2;
  phantomx::aggregator::AggregationEngine engine;
  for (int i=0;i<50;++i) {
    phantomx::aggregator::VerifierAttestation a;
    a.verifier_pubkey = id(static_cast<std::uint8_t>(i+1));
    a.assignment_id = req.assignment_id; a.subject_id=req.subject_id; a.evidence_hash=req.evidence_hash;
    a.subject_type=req.subject_type; a.result=phantomx::aggregator::AttestationResult::kAccept;
    a.timestamp=10; a.expires_at=100;
    for (auto& b: a.signature) b = static_cast<std::uint8_t>(rng()&0xff);
    auto r = engine.submit_attestation(req,a,11);
    PHX_EXPECT(!r.ok()); // random sig must be rejected
  }
}

PHX_TEST(fuzz_consensus_invalid_votes_rejected) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i=0;i<4;++i){ auto kp=phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  phantomx::consensus::ValidatorSet vs; vs.epoch=0;
  for (auto& k: keys){ phantomx::consensus::Validator v; v.validator_pubkey=k.pub.bytes; v.operator_pubkey=k.pub.bytes; v.voting_power=10; v.active=true; vs.validators.push_back(v); vs.total_voting_power+=10; }
  phantomx::consensus::BftEngine engine(vs);
  std::mt19937 rng(99);
  for (int i=0;i<100;++i) {
    phantomx::consensus::Vote v;
    v.phase = (i%2==0)?phantomx::consensus::VotePhase::kPrevote:phantomx::consensus::VotePhase::kPrecommit;
    v.epoch=0; v.view=0; v.height=1; v.block_hash=h(static_cast<std::uint8_t>(i)); v.voting_power=10;
    v.validator_pubkey = keys[static_cast<std::size_t>(i%4)].pub.bytes;
    for (auto& b: v.signature) b = static_cast<std::uint8_t>(rng()&0xff); // bad sig
    auto r = engine.submit_vote(v);
    PHX_EXPECT(!r.ok());
  }
}

PHX_TEST(chaos_consensus_timeout_view_advance) {
  std::vector<phantomx::crypto::KeyPair> keys;
  for (int i=0;i<4;++i){ auto kp=phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity); PHX_EXPECT(kp.ok()); keys.push_back(kp.value()); }
  phantomx::consensus::ValidatorSet vs; vs.epoch=0;
  for (auto& k: keys){ phantomx::consensus::Validator v; v.validator_pubkey=k.pub.bytes; v.operator_pubkey=k.pub.bytes; v.voting_power=10; v.active=true; vs.validators.push_back(v); vs.total_voting_power+=10; }
  phantomx::consensus::BftEngine engine(vs);
  for (int i=0;i<20;++i) {
    auto r = engine.timeout(static_cast<std::uint64_t>(i));
    PHX_EXPECT(r.ok());
    PHX_EXPECT(engine.state().view == static_cast<std::uint64_t>(i+1));
  }
}
