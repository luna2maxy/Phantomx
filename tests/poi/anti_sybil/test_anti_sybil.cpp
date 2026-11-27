#include <array>
#include <string>
#include <vector>

#include "../../test_framework.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/poi/anti_sybil/anti_sybil.hpp"
#include "phantomx/poi/challenge/challenge.hpp"
#include "phantomx/poi/evidence/evidence.hpp"
#include "phantomx/poi/identity/identity.hpp"

namespace {

phantomx::poi::NodeIdentity make_identity(const phantomx::crypto::KeyPair& node, const phantomx::crypto::KeyPair& op,
                                          std::string wallet, std::string endpoint, std::uint64_t now) {
  phantomx::poi::NodeIdentity id;
  id.node_pubkey = node.pub.bytes;
  id.operator_pubkey = op.pub.bytes;
  id.wallet_address = std::move(wallet);
  id.network_endpoint = std::move(endpoint);
  id.registered_at = now;
  id.expires_at = now + 3600;
  id.registration_nonce.fill(static_cast<std::uint8_t>(now & 0xFF));
  auto signed_id = phantomx::poi::sign_identity(id, node.sec);
  PHX_EXPECT(signed_id.ok());
  return signed_id.value();
}

}  // namespace

PHX_TEST(anti_sybil_register_and_duplicate) {
  auto node = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  auto op = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(node.ok() && op.ok());
  auto id = make_identity(node.value(), op.value(), "PhantomX1wallet", "10.0.0.2:8742", 100);
  phantomx::poi::AntiSybilRegistry reg;
  PHX_EXPECT(reg.register_identity(id, 100).ok());
  PHX_EXPECT(!reg.register_identity(id, 100).ok()); // duplicate
}

PHX_TEST(anti_sybil_operator_cap_vps_farm) {
  auto op = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(op.ok());
  phantomx::poi::AntiSybilConfig cfg;
  cfg.max_nodes_per_operator = 2;
  phantomx::poi::AntiSybilRegistry reg(cfg);
  for (int i = 0; i < 2; ++i) {
    auto node = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(node.ok());
    auto id = make_identity(node.value(), op.value(), "PhantomX1w"+std::to_string(i), "10.0.1."+std::to_string(i)+":8742", 100);
    PHX_EXPECT(reg.register_identity(id, 100).ok());
  }
  auto node3 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(node3.ok());
  auto id3 = make_identity(node3.value(), op.value(), "PhantomX1w3", "10.0.1.3:8742", 100);
  PHX_EXPECT(!reg.register_identity(id3, 100).ok()); // VPS farm / many identities one operator
}

PHX_TEST(anti_sybil_replay_nonce_rejected) {
  phantomx::poi::NonceRegistry reg;
  std::array<std::uint8_t, 32> nonce{};
  nonce[0] = 9;
  PHX_EXPECT(reg.mark_used(nonce, 1000));
  PHX_EXPECT(!reg.mark_used(nonce, 1001)); // replay
}

PHX_TEST(anti_sybil_fake_uptime_bandwidth_requires_attestation) {
  phantomx::poi::EvidenceRecord e;
  e.kind = phantomx::poi::EvidenceKind::kUptime;
  e.measured_value_a = 100; e.measured_value_b = 100;
  e.component_score = 100000;
  e.simulator = false;
  // self-reported/no attestation -> not acceptable as final proof
  PHX_EXPECT(!phantomx::poi::evidence_has_independent_attestation(e, 1));
}

PHX_TEST(anti_sybil_verifier_pool_collusion_signal) {
  std::vector<phantomx::poi::NodeIdentity> ids;
  auto op = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(op.ok());
  for (int i = 0; i < 4; ++i) {
    auto node = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(node.ok());
    ids.push_back(make_identity(node.value(), op.value(), "PhantomX1same", "10.0.2."+std::to_string(i)+":8742", 100));
  }
  auto signals = phantomx::poi::detect_collusion_signals(ids);
  bool found = false;
  for (const auto& s : signals) if (s.code == "collusion_operator_cluster" || s.code == "collusion_wallet_reuse") found = true;
  PHX_EXPECT(found);
}

PHX_TEST(anti_sybil_compromised_identity_rotation) {
  auto old_node = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  auto new_node = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  auto op = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(old_node.ok() && new_node.ok() && op.ok());
  auto id = make_identity(old_node.value(), op.value(), "PhantomX1w", "10.0.0.5:8742", 100);
  phantomx::poi::AntiSybilRegistry reg;
  PHX_EXPECT(reg.register_identity(id, 100).ok());

  phantomx::poi::IdentityRotation rot;
  rot.old_node_pubkey = old_node.value().pub.bytes;
  rot.new_node_pubkey = new_node.value().pub.bytes;
  rot.rotated_at = 120;
  rot.nonce.fill(1);
  auto signed_rot = phantomx::poi::sign_rotation(rot, old_node.value().sec, new_node.value().sec);
  PHX_EXPECT(signed_rot.ok());
  auto new_id = make_identity(new_node.value(), op.value(), "PhantomX1w", "10.0.0.5:8742", 120);
  PHX_EXPECT(reg.rotate_identity(signed_rot.value(), new_id, 120).ok());
  PHX_EXPECT(!reg.has_identity(old_node.value().pub.bytes));
  PHX_EXPECT(reg.has_identity(new_node.value().pub.bytes));
}

PHX_TEST(anti_sybil_evidence_freshness) {
  PHX_EXPECT(phantomx::poi::evidence_fresh(100, 150, 60));
  PHX_EXPECT(!phantomx::poi::evidence_fresh(100, 200, 60)); // stale
}

int main() { return phantomx::test::run_all(); }
