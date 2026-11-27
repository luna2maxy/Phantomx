#include <array>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/poi/challenge/challenge.hpp"
#include "phantomx/poi/evidence/evidence.hpp"
#include "phantomx/verifier/assignment.hpp"
#include "phantomx/verifier/registry.hpp"
#include "phantomx/verifier/service.hpp"

namespace {
phantomx::verifier::VerifierRecord make_verifier(const phantomx::crypto::KeyPair& kp, phantomx::verifier::VerifierType type) {
  phantomx::verifier::VerifierRecord v;
  v.verifier_pubkey = kp.pub.bytes;
  v.operator_pubkey.fill(static_cast<std::uint8_t>(kp.pub.bytes[0]));
  v.type = type;
  v.status = phantomx::verifier::VerifierStatus::kActive;
  return v;
}
}  // namespace

PHX_TEST(verifier_offline_not_eligible) {
  auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  PHX_EXPECT(kp.ok());
  phantomx::verifier::VerifierRegistry reg;
  auto v = make_verifier(kp.value(), phantomx::verifier::VerifierType::kPoi);
  PHX_EXPECT(reg.register_verifier(v, 100).ok());
  PHX_EXPECT(reg.mark_offline(v.verifier_pubkey, 120).ok());
  auto e = reg.eligible_for(v.verifier_pubkey, phantomx::verifier::VerifierType::kPoi);
  PHX_EXPECT(!e.eligible);
}

PHX_TEST(verifier_malicious_self_verification_forbidden) {
  auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  PHX_EXPECT(kp.ok());
  phantomx::verifier::VerifierService svc;
  std::array<std::uint8_t, 32> subject = kp.value().pub.bytes; // same as verifier
  auto task = svc.create_challenge(phantomx::verifier::VerifierType::kPoi, subject, kp.value().pub.bytes, 100, 60, {});
  PHX_EXPECT(task.ok());
  phantomx::poi::EvidenceRecord ev;
  ev.subject_pubkey = subject;
  ev.simulator = false;
  auto r = svc.verify_and_sign(task.value(), ev, true, kp.value().sec, kp.value().pub, 110);
  PHX_EXPECT(!r.ok()); // self-verification forbidden
}

PHX_TEST(verifier_replay_nonce_rejected) {
  phantomx::poi::NonceRegistry nonces;
  std::array<std::uint8_t, 32> nonce{}; nonce[0] = 7;
  PHX_EXPECT(nonces.mark_used(nonce, 100));
  PHX_EXPECT(!nonces.mark_used(nonce, 101));
}

PHX_TEST(verifier_collusion_conflict_detected) {
  auto a = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto b = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto c = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  PHX_EXPECT(a.ok() && b.ok() && c.ok());
  phantomx::verifier::VerifierRegistry reg;
  auto va = make_verifier(a.value(), phantomx::verifier::VerifierType::kPoi);
  auto vb = make_verifier(b.value(), phantomx::verifier::VerifierType::kPoi);
  auto vc = make_verifier(c.value(), phantomx::verifier::VerifierType::kPoi);
  va.operator_pubkey.fill(1); vb.operator_pubkey.fill(2); vc.operator_pubkey.fill(3);
  PHX_EXPECT(reg.register_verifier(va, 1).ok());
  PHX_EXPECT(reg.register_verifier(vb, 1).ok());
  PHX_EXPECT(reg.register_verifier(vc, 1).ok());

  phantomx::verifier::AssignmentRequest req;
  req.type = phantomx::verifier::VerifierType::kPoi;
  req.subject_pubkey.fill(9);
  req.subject_operator_pubkey.fill(1); // same as verifier A operator
  req.required_verifiers = 2;
  req.randomness_seed.fill(5);
  auto asn = phantomx::verifier::assign_verifiers(reg, req);
  PHX_EXPECT(asn.ok());
  for (const auto& vpk : asn.value().verifiers) {
    PHX_EXPECT(!(vpk == va.verifier_pubkey)); // conflict verifier excluded
  }
}

PHX_TEST(verifier_invalid_evidence_rejected) {
  auto verifier = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto subject = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(verifier.ok() && subject.ok());
  phantomx::verifier::VerifierService svc;
  auto task = svc.create_challenge(phantomx::verifier::VerifierType::kPoi, subject.value().pub.bytes, verifier.value().pub.bytes, 100, 60, {});
  PHX_EXPECT(task.ok());
  phantomx::poi::EvidenceRecord ev;
  ev.subject_pubkey = subject.value().pub.bytes;
  ev.simulator = true; // simulator evidence not allowed
  auto r = svc.verify_and_sign(task.value(), ev, true, verifier.value().sec, verifier.value().pub, 110);
  PHX_EXPECT(!r.ok());
}

PHX_TEST(verifier_signed_result_verifies) {
  auto verifier = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto subject = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(verifier.ok() && subject.ok());
  phantomx::verifier::VerifierService svc;
  auto task = svc.create_challenge(phantomx::verifier::VerifierType::kPoi, subject.value().pub.bytes, verifier.value().pub.bytes, 100, 60, {});
  PHX_EXPECT(task.ok());
  phantomx::poi::EvidenceRecord ev;
  ev.subject_pubkey = subject.value().pub.bytes;
  ev.simulator = false;
  auto r = svc.verify_and_sign(task.value(), ev, true, verifier.value().sec, verifier.value().pub, 110);
  PHX_EXPECT(r.ok());
  PHX_EXPECT(phantomx::verifier::verify_result_signature(r.value()));
}

int main() { return phantomx::test::run_all(); }
