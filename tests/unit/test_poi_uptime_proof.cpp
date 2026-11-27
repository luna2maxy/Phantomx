#include "../test_framework.hpp"

#include <unordered_set>
#include <vector>

#include "phantomx/common/crypto.hpp"
#include "phantomx/poi/uptime_proof.hpp"

using namespace phantomx;
using namespace phantomx::poi;

namespace {
crypto::KeyPair kp(crypto::KeyRole role) {
  auto r = crypto::generate_keypair(role);
  PHX_EXPECT(r.ok());
  return r.value();
}

PoiChallengeV1 mk_challenge(const crypto::KeyPair& verifier, std::uint64_t now_ms) {
  PoiChallengeV1 c;
  c.epoch = 1; c.slot = 10; c.issued_at_ms = now_ms; c.expires_at_ms = now_ms + 60000;
  c.verifier_pubkey = verifier.pub.bytes;
  c.machine_id.fill(0x11);
  c.owner_address = "ownerA";
  c.nonce.fill(0x22);
  c.params_hash.fill(0x33);
  auto s = sign_poi_challenge(c, verifier.sec);
  PHX_EXPECT(s.ok());
  return c;
}
}

PHX_TEST(poi_uptime_challenge_sign_verify_and_freshness) {
  PHX_EXPECT(crypto::init().ok());
  auto v = kp(crypto::KeyRole::kVerifierIdentity);
  auto c = mk_challenge(v, 100000);
  PHX_EXPECT(verify_poi_challenge(c, 120000).ok());
  PHX_EXPECT(!verify_poi_challenge(c, 100000 + 60000 + 300001).ok()); // expired beyond skew

  // tamper
  c.nonce[0] ^= 1;
  PHX_EXPECT(!verify_poi_challenge(c, 120000).ok());
}

PHX_TEST(poi_uptime_response_and_attestation_binding) {
  PHX_EXPECT(crypto::init().ok());
  auto v = kp(crypto::KeyRole::kVerifierIdentity);
  auto m = kp(crypto::KeyRole::kNodeIdentity);
  auto c = mk_challenge(v, 100000);

  PoiResponseV1 r;
  r.challenge_id = c.challenge_id; r.epoch=c.epoch; r.slot=c.slot; r.machine_id=c.machine_id;
  r.machine_pubkey = m.pub.bytes; r.response_hash.fill(0x44); r.responded_at_ms=110000;
  PHX_EXPECT(sign_poi_response(r, m.sec).ok());
  PHX_EXPECT(verify_poi_response(r, c, 120000).ok());
  r.responded_at_ms = 200000;
  PHX_EXPECT(!verify_poi_response(r, c, 200000).ok()); // late

  PoiAttestationV1 a;
  a.challenge_id=c.challenge_id; a.epoch=c.epoch; a.slot=c.slot; a.machine_id=c.machine_id;
  a.verifier_pubkey=v.pub.bytes; a.pass=true; a.score_uptime_milli=100000; a.observed_at_ms=115000; a.evidence_hash.fill(0x55);
  PHX_EXPECT(sign_poi_attestation(a, v.sec).ok());
  PHX_EXPECT(verify_poi_attestation(a, c, 120000).ok());
}

PHX_TEST(poi_uptime_quorum_replay_conflict_and_invalid) {
  PHX_EXPECT(crypto::init().ok());
  auto v1 = kp(crypto::KeyRole::kVerifierIdentity);
  auto v2 = kp(crypto::KeyRole::kVerifierIdentity);
  auto v3 = kp(crypto::KeyRole::kVerifierIdentity);
  auto c = mk_challenge(v1, 100000);
  // set challenge verifier pubkey to v1 but attestations come from v1,v2,v3 - allowed if in active set
  std::unordered_set<std::string> active;
  auto hexpk=[](const crypto::PublicKey& p){ std::string s; static const char* h="0123456789abcdef"; s.resize(64); for(size_t i=0;i<32;++i){s[i*2]=h[p.bytes[i]>>4]; s[i*2+1]=h[p.bytes[i]&0xF];} return s; };
  active.insert(hexpk(v1.pub)); active.insert(hexpk(v2.pub)); active.insert(hexpk(v3.pub));

  auto mk_att=[&](const crypto::KeyPair& v, bool pass, uint32_t score){
    PoiAttestationV1 a; a.challenge_id=c.challenge_id; a.epoch=c.epoch; a.slot=c.slot; a.machine_id=c.machine_id;
    a.verifier_pubkey=v.pub.bytes; a.pass=pass; a.score_uptime_milli=score; a.observed_at_ms=115000; a.evidence_hash.fill(pass?0x55:0x66);
    PHX_EXPECT(sign_poi_attestation(a, v.sec).ok()); return a;
  };

  std::vector<PoiAttestationV1> atts{mk_att(v1,true,100000), mk_att(v2,true,90000), mk_att(v3,false,0)};
  auto res = aggregate_poi_uptime_attestations(c, atts, active, 2, 120000);
  PHX_EXPECT(res.quorum_met);
  PHX_EXPECT_EQ(static_cast<long long>(res.pass_count), 2);
  PHX_EXPECT_EQ(static_cast<long long>(res.score_uptime_milli), 100000); // median of 90000,100000

  // duplicate
  atts.push_back(atts[0]);
  auto res2 = aggregate_poi_uptime_attestations(c, atts, active, 2, 120000);
  PHX_EXPECT_EQ(static_cast<long long>(res2.duplicate_count), 1);

  // conflict
  auto conflict = mk_att(v1, false, 0);
  std::vector<PoiAttestationV1> atts3{mk_att(v1,true,100000), conflict};
  auto res3 = aggregate_poi_uptime_attestations(c, atts3, active, 2, 120000);
  PHX_EXPECT_EQ(static_cast<long long>(res3.conflict_count), 1);

  // replay cache
  PoiUptimeReplayCache cache;
  PHX_EXPECT(cache.mark_challenge(c.challenge_id));
  PHX_EXPECT(!cache.mark_challenge(c.challenge_id));
  PHX_EXPECT(cache.mark_attestation(c.challenge_id, v1.pub.bytes));
  PHX_EXPECT(!cache.mark_attestation(c.challenge_id, v1.pub.bytes));
}
