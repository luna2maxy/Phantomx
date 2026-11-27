#include <array>
#include <cstdint>

#include "../test_framework.hpp"
#include "phantomx/aggregator/aggregator.hpp"
#include "phantomx/common/crypto.hpp"

namespace {
std::array<std::uint8_t, 32> id32(std::uint8_t b) { std::array<std::uint8_t, 32> o{}; o[0] = b; return o; }
phantomx::Hash256Bytes hash32(std::uint8_t b) { phantomx::Hash256Bytes h{}; h[0] = b; return h; }

phantomx::aggregator::VerifierAttestation make_att(const phantomx::crypto::KeyPair& kp,
                                                   const std::array<std::uint8_t, 32>& assignment,
                                                   const std::array<std::uint8_t, 32>& subject,
                                                   const phantomx::Hash256Bytes& ev,
                                                   phantomx::aggregator::AttestationResult result,
                                                   std::uint64_t now) {
  phantomx::aggregator::VerifierAttestation a;
  a.verifier_pubkey = kp.pub.bytes;
  a.assignment_id = assignment;
  a.subject_id = subject;
  a.evidence_hash = ev;
  a.subject_type = phantomx::aggregator::SubjectType::kPoiEvidence;
  a.result = result;
  a.timestamp = now;
  a.expires_at = now + 600;
  auto h = phantomx::aggregator::attestation_hash(a);
  auto sig = phantomx::crypto::sign(std::span<const std::uint8_t>(h.data(), h.size()), kp.sec);
  PHX_EXPECT(sig.ok());
  a.signature = sig.value().bytes;
  return a;
}
}  // namespace

PHX_TEST(aggregator_certificate_quorum_and_verify) {
  auto v1 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto v2 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto ag = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(v1.ok() && v2.ok() && ag.ok());

  phantomx::aggregator::AggregationRequest req;
  req.network_id = "regtest";
  req.epoch_id = 1;
  req.assignment_id = id32(0xA1);
  req.subject_id = id32(0xB2);
  req.evidence_hash = hash32(0xC3);
  req.quorum.required = 2;

  phantomx::aggregator::AggregationEngine engine;
  auto a1 = make_att(v1.value(), req.assignment_id, req.subject_id, req.evidence_hash, phantomx::aggregator::AttestationResult::kAccept, 100);
  auto a2 = make_att(v2.value(), req.assignment_id, req.subject_id, req.evidence_hash, phantomx::aggregator::AttestationResult::kAccept, 101);
  PHX_EXPECT(engine.submit_attestation(req, a1, 102).ok());
  PHX_EXPECT(engine.submit_attestation(req, a2, 102).ok());

  auto cert = engine.build_certificate(req, ag.value().sec, ag.value().pub.bytes, 103);
  PHX_EXPECT(cert.ok());
  PHX_EXPECT(cert.value().quorum_achieved == 2);
  PHX_EXPECT(cert.value().result == phantomx::aggregator::AttestationResult::kAccept);
  PHX_EXPECT(engine.verify_certificate(cert.value()));
}

PHX_TEST(aggregator_rejects_duplicate_and_invalid_signature) {
  auto v1 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  PHX_EXPECT(v1.ok());
  phantomx::aggregator::AggregationRequest req;
  req.network_id = "regtest"; req.assignment_id = id32(1); req.subject_id = id32(2); req.evidence_hash = hash32(3); req.quorum.required = 2;
  phantomx::aggregator::AggregationEngine engine;
  auto a1 = make_att(v1.value(), req.assignment_id, req.subject_id, req.evidence_hash, phantomx::aggregator::AttestationResult::kAccept, 10);
  PHX_EXPECT(engine.submit_attestation(req, a1, 11).ok());
  auto dup = a1;
  PHX_EXPECT(!engine.submit_attestation(req, dup, 11).ok()); // duplicate verifier
  auto bad = a1;
  bad.signature[0] ^= 0x01;
  bad.verifier_pubkey = id32(9); // different verifier but invalid sig
  PHX_EXPECT(!engine.submit_attestation(req, bad, 11).ok());
}

PHX_TEST(aggregator_insufficient_quorum_fails) {
  auto v1 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto ag = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(v1.ok() && ag.ok());
  phantomx::aggregator::AggregationRequest req;
  req.network_id = "regtest"; req.assignment_id = id32(7); req.subject_id = id32(8); req.evidence_hash = hash32(9); req.quorum.required = 2;
  phantomx::aggregator::AggregationEngine engine;
  auto a1 = make_att(v1.value(), req.assignment_id, req.subject_id, req.evidence_hash, phantomx::aggregator::AttestationResult::kAccept, 10);
  PHX_EXPECT(engine.submit_attestation(req, a1, 11).ok());
  auto cert = engine.build_certificate(req, ag.value().sec, ag.value().pub.bytes, 12);
  PHX_EXPECT(!cert.ok());
}

PHX_TEST(aggregator_certificate_store_and_lookup) {
  auto v1 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto v2 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto ag = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(v1.ok() && v2.ok() && ag.ok());
  phantomx::aggregator::AggregationRequest req;
  req.network_id="regtest"; req.assignment_id=id32(0xAA); req.subject_id=id32(0xBB); req.evidence_hash=hash32(0xCC); req.quorum.required=2;
  phantomx::aggregator::AggregationEngine engine;
  PHX_EXPECT(engine.submit_attestation(req, make_att(v1.value(),req.assignment_id,req.subject_id,req.evidence_hash,phantomx::aggregator::AttestationResult::kAccept,10),11).ok());
  PHX_EXPECT(engine.submit_attestation(req, make_att(v2.value(),req.assignment_id,req.subject_id,req.evidence_hash,phantomx::aggregator::AttestationResult::kAccept,10),11).ok());
  auto cert = engine.build_certificate(req, ag.value().sec, ag.value().pub.bytes, 12);
  PHX_EXPECT(cert.ok());
  auto h = phantomx::aggregator::certificate_hash(cert.value());
  const auto* stored = engine.get_certificate(h);
  PHX_EXPECT(stored != nullptr);
  PHX_EXPECT(engine.verify_certificate(*stored));
}

PHX_TEST(aggregator_dispute_open_resolve) {
  phantomx::aggregator::AggregationEngine engine;
  auto rep = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  PHX_EXPECT(rep.ok());
  auto d = engine.open_dispute(id32(1), rep.value().pub.bytes, "conflict", 100);
  PHX_EXPECT(d.ok());
  PHX_EXPECT(engine.disputes().size() == 1);
  PHX_EXPECT(engine.resolve_dispute(d.value().dispute_id, phantomx::aggregator::DisputeStatus::kAccepted).ok());
  PHX_EXPECT(engine.disputes()[0].status == phantomx::aggregator::DisputeStatus::kAccepted);
}

PHX_TEST(aggregator_sqlite_persistence_roundtrip) {
  auto v1 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto v2 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto ag = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(v1.ok() && v2.ok() && ag.ok());
  phantomx::aggregator::AggregationRequest req;
  req.network_id="regtest"; req.assignment_id=id32(0xAB); req.subject_id=id32(0xBC); req.evidence_hash=hash32(0xCD); req.quorum.required=2;
  phantomx::aggregator::AggregationEngine engine;
  PHX_EXPECT(engine.submit_attestation(req, make_att(v1.value(),req.assignment_id,req.subject_id,req.evidence_hash,phantomx::aggregator::AttestationResult::kAccept,10),11).ok());
  PHX_EXPECT(engine.submit_attestation(req, make_att(v2.value(),req.assignment_id,req.subject_id,req.evidence_hash,phantomx::aggregator::AttestationResult::kAccept,10),11).ok());
  auto cert = engine.build_certificate(req, ag.value().sec, ag.value().pub.bytes, 12);
  PHX_EXPECT(cert.ok());
  const auto path = std::string("/tmp/opencode/agg_test.db");
  PHX_EXPECT(engine.save_state_db(path).ok());
  phantomx::aggregator::AggregationEngine engine2;
  PHX_EXPECT(engine2.load_state_db(path).ok());
  auto h = phantomx::aggregator::certificate_hash(cert.value());
  const auto* stored = engine2.get_certificate(h);
  PHX_EXPECT(stored != nullptr);
  PHX_EXPECT(engine2.verify_certificate(*stored));
}
