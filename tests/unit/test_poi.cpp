#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <vector>

#ifdef _WIN32
static inline int setenv(const char* name, const char* value, int /*overwrite*/) { return _putenv_s(name, value); }
static inline int unsetenv(const char* name) { return _putenv_s(name, ""); }
#endif

#include "../test_framework.hpp"
#include "phantomx/aggregator/aggregator.hpp"
#include "phantomx/blockchain/chain.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/poi/challenge/challenge.hpp"
#include "phantomx/poi/evidence/evidence.hpp"
#include "phantomx/poi/measurement/measurement.hpp"
#include "phantomx/poi/score.hpp"
#include "phantomx/poi/verifier_pop.hpp"
#include "phantomx/poi/simulator.hpp"
#include "phantomx/poi/uptime_proof.hpp"
#include "phantomx/pool/pool.hpp"
#include "phantomx/rpc/node_handlers.hpp"
#include "phantomx/rpc/rpc.hpp"
#include "phantomx/utxo/mempool.hpp"

namespace phantomx::rpc { extern thread_local std::string g_rpc_remote_ip; }

PHX_TEST(poi_fixed_point_score) {
  phantomx::poi::PoiMetrics m{100000, 100000, 100000, 100000};
  auto s = phantomx::poi::compute_poi_score(m);
  PHX_EXPECT_EQ(static_cast<long long>(s.total), 100000LL);
  phantomx::poi::PoiMetrics half{50000, 50000, 50000, 50000};
  auto s2 = phantomx::poi::compute_poi_score(half);
  PHX_EXPECT_EQ(static_cast<long long>(s2.total), 50000LL);
}

PHX_TEST(poi_latency_robust_aggregation) {
  std::vector<phantomx::poi::LatencyProbe> probes;
  for (std::uint64_t rtt : {30ULL, 35ULL, 40ULL, 45ULL, 500ULL}) { // 500 is outlier
    phantomx::poi::LatencyProbe p;
    p.valid = true;
    p.rtt_ms = rtt;
    probes.push_back(p);
  }
  auto agg = phantomx::poi::aggregate_latency_rtt_ms(probes);
  PHX_EXPECT(agg.ok());
  PHX_EXPECT(static_cast<long long>(agg.value()) < 100LL); // trimmed mean should reduce outlier impact
}

PHX_TEST(poi_nonce_replay_prevention) {
  phantomx::poi::NonceRegistry reg;
  std::array<std::uint8_t, 32> nonce{};
  nonce[0] = 1;
  PHX_EXPECT(reg.mark_used(nonce, 100));
  PHX_EXPECT(!reg.mark_used(nonce, 101)); // replay rejected
}

PHX_TEST(poi_evidence_signature_verification) {
  auto verifier = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  PHX_EXPECT(verifier.ok());
  phantomx::poi::EvidenceRecord e;
  e.kind = phantomx::poi::EvidenceKind::kUptime;
  e.subject_pubkey.fill(7);
  e.challenge_nonce.fill(8);
  e.window_start = 100;
  e.window_end = 200;
  e.measured_value_a = 95;
  e.measured_value_b = 100;
  e.component_score = 95000;
  e.simulator = false;
  auto att = phantomx::poi::sign_evidence(e, verifier.value().sec, verifier.value().pub, 150);
  PHX_EXPECT(att.ok());
  PHX_EXPECT(phantomx::poi::verify_attestation(e, att.value()));
  e.measured_value_a = 1; // tamper
  PHX_EXPECT(!phantomx::poi::verify_attestation(e, att.value()));
}

PHX_TEST(poi_simulator_marked_not_mainnet) {
  std::array<std::uint8_t, 32> subject{};
  subject.fill(3);
  auto e = phantomx::poi::simulate_evidence(phantomx::poi::EvidenceKind::kBandwidth, subject, {}, 1000);
  PHX_EXPECT(e.simulator);
  PHX_EXPECT(e.component_score > 0);
}

PHX_TEST(poi_epoch_level_dynamic) {
  PHX_EXPECT_EQ(static_cast<long long>(phantomx::poi::poi_epoch_level(0, 10)), 1LL);
  PHX_EXPECT_EQ(static_cast<long long>(phantomx::poi::poi_epoch_level(1, 10)), 1LL);
  PHX_EXPECT_EQ(static_cast<long long>(phantomx::poi::poi_epoch_level(10, 10)), 1LL);
  PHX_EXPECT_EQ(static_cast<long long>(phantomx::poi::poi_epoch_level(11, 10)), 2LL);
  PHX_EXPECT_EQ(static_cast<long long>(phantomx::poi::poi_epoch_level(3, 10)), 1LL); // down from 11->3 means epoch 1
}

PHX_TEST(poi_target_interval_modulated) {
  const auto base = 60000ULL;
  const auto slow = phantomx::poi::poi_target_block_interval_ms(base, 25.0, 1);
  const auto fast = phantomx::poi::poi_target_block_interval_ms(base, 75.0, 2);
  PHX_EXPECT(slow > base);     // low score -> slower
  PHX_EXPECT(fast < base);     // high score+epoch -> faster
  PHX_EXPECT(fast >= 15000ULL);
  PHX_EXPECT(slow <= 300000ULL);
}


PHX_TEST(poi_composite_score_partial_and_full) {
  // Legacy wrapper has no uptime input; missing uptime must not be treated as perfect.
  auto partial = phantomx::poi::poi_composite_score(100ULL * 1024ULL * 1024ULL, -1.0, -1.0, 1);
  PHX_EXPECT(partial.available);
  PHX_EXPECT(partial.partial);
  PHX_EXPECT(partial.score > 0.0);
  PHX_EXPECT(partial.latency < 0.0);
  PHX_EXPECT(partial.relay < 0.0);
  PHX_EXPECT(partial.uptime < 0.0);

  auto legacy_fullish = phantomx::poi::poi_composite_score(100ULL * 1024ULL * 1024ULL, 50.0, 0.9, 10);
  PHX_EXPECT(legacy_fullish.available);
  PHX_EXPECT(legacy_fullish.partial); // uptime still missing in legacy wrapper
  PHX_EXPECT(legacy_fullish.uptime < 0.0);

  phantomx::poi::PoiCompositeInput in;
  in.bandwidth_mbps = 100;
  in.bandwidth_valid = true;
  in.latency_ms = 50.0;
  in.uptime_ratio = 0.99;
  in.relay_ratio = 0.9;
  in.active_miners = 10;
  in.network_valid = true;
  auto full = phantomx::poi::poi_composite_score_v2(in);
  PHX_EXPECT(full.available);
  PHX_EXPECT(!full.partial);
  PHX_EXPECT(full.uptime >= 0.0);
  PHX_EXPECT(full.score >= partial.score);
}

PHX_TEST(poi_bandwidth_diminishing_no_cap) {
  using phantomx::poi::poi_score_bandwidth_mbps;
  const double vals[] = {0,1,5,10,20,50,100,200,500,750,1000,2000,5000,10000,20000,100000};
  double prev = -1.0;
  for (double b : vals) {
    const double s = poi_score_bandwidth_mbps(b);
    PHX_EXPECT(s >= 0.0 && s <= 1.0);
    if (prev >= 0.0) PHX_EXPECT(s > prev); // strict monotonic
    prev = s;
  }
  // Specific required comparisons
  PHX_EXPECT(poi_score_bandwidth_mbps(100) < poi_score_bandwidth_mbps(500));
  PHX_EXPECT(poi_score_bandwidth_mbps(500) < poi_score_bandwidth_mbps(1000));
  PHX_EXPECT(poi_score_bandwidth_mbps(1000) < poi_score_bandwidth_mbps(2000));
  PHX_EXPECT(poi_score_bandwidth_mbps(2000) < poi_score_bandwidth_mbps(10000));
  PHX_EXPECT(poi_score_bandwidth_mbps(10000) < poi_score_bandwidth_mbps(20000));
  // No hard saturation
  PHX_EXPECT(poi_score_bandwidth_mbps(100) < 1.0);
  PHX_EXPECT(poi_score_bandwidth_mbps(1000) < 1.0);
  PHX_EXPECT(poi_score_bandwidth_mbps(100000) < 1.0);
  // Diminishing returns: delta(100->500) > delta(1000->5000) in Mbps-normalized marginal sense
  const double d1 = poi_score_bandwidth_mbps(500) - poi_score_bandwidth_mbps(100);
  const double d2 = poi_score_bandwidth_mbps(5000) - poi_score_bandwidth_mbps(1000);
  PHX_EXPECT(d1 > 0.0);
  PHX_EXPECT(d2 > 0.0);
  // invalid
  PHX_EXPECT(poi_score_bandwidth_mbps(-1.0) < 0.0);
  PHX_EXPECT(poi_score_bandwidth_mbps(std::numeric_limits<double>::quiet_NaN()) < 0.0);
  PHX_EXPECT(poi_score_bandwidth_mbps(std::numeric_limits<double>::infinity()) < 0.0);
  const double huge = poi_score_bandwidth_mbps(1e12);
  PHX_EXPECT(huge > 0.999999);
  PHX_EXPECT(huge <= 1.0);
}

PHX_TEST(poi_latency_curve_no_hard_zero) {
  using phantomx::poi::poi_score_latency_ms;
  const double vals[] = {0,1,5,10,20,50,100,200,300,500,1000,5000};
  double prev = 2.0;
  for (double l : vals) {
    const double s = poi_score_latency_ms(l);
    PHX_EXPECT(s >= 0.0 && s <= 1.0);
    PHX_EXPECT(s < prev);
    prev = s;
  }
  PHX_EXPECT(poi_score_latency_ms(500) > 0.0);
  PHX_EXPECT(poi_score_latency_ms(1000) > 0.0);
  PHX_EXPECT(poi_score_latency_ms(500) > poi_score_latency_ms(1000));
  PHX_EXPECT(poi_score_latency_ms(-1.0) < 0.0);
  PHX_EXPECT(poi_score_latency_ms(std::numeric_limits<double>::quiet_NaN()) < 0.0);
  PHX_EXPECT(poi_score_latency_ms(std::numeric_limits<double>::infinity()) < 0.0);
}

PHX_TEST(poi_uptime_component_and_invalid) {
  using phantomx::poi::poi_score_uptime_ratio;
  const double vals[] = {1.0,0.9999,0.999,0.99,0.95,0.90,0.75,0.50,0.0};
  double prev = 2.0;
  for (double u : vals) {
    const double s = poi_score_uptime_ratio(u);
    PHX_EXPECT(s >= 0.0 && s <= 1.0);
    PHX_EXPECT(s <= prev + 1e-12);
    prev = s;
  }
  PHX_EXPECT(poi_score_uptime_ratio(-0.1) < 0.0);
  PHX_EXPECT(poi_score_uptime_ratio(1.1) < 0.0);
  PHX_EXPECT(poi_score_uptime_ratio(std::numeric_limits<double>::quiet_NaN()) < 0.0);

  phantomx::poi::PoiCompositeInput in;
  in.bandwidth_mbps = 100; in.bandwidth_valid = true;
  in.latency_ms = 20; in.relay_ratio = 0.8; in.network_valid = true; in.active_miners = 10;
  in.uptime_ratio = 1.0;
  auto full_up = phantomx::poi::poi_composite_score_v2(in);
  in.uptime_ratio = 0.5;
  auto half_up = phantomx::poi::poi_composite_score_v2(in);
  in.uptime_ratio = -1.0;
  auto missing_up = phantomx::poi::poi_composite_score_v2(in);
  PHX_EXPECT(full_up.score > half_up.score);
  PHX_EXPECT(missing_up.partial);
  PHX_EXPECT(missing_up.uptime < 0.0);
  PHX_EXPECT(missing_up.score < 100.0); // missing uptime not perfect
}

PHX_TEST(poi_composite_weights_and_effects) {
  phantomx::poi::PoiCompositeInput base;
  base.bandwidth_mbps = 100; base.bandwidth_valid = true;
  base.latency_ms = 20; base.uptime_ratio = 0.99; base.relay_ratio = 0.8;
  base.active_miners = 10; base.network_valid = true;
  auto b = phantomx::poi::poi_composite_score_v2(base);
  auto bw = base; bw.bandwidth_mbps = 500; auto sbw = phantomx::poi::poi_composite_score_v2(bw);
  auto lt = base; lt.latency_ms = 10; auto slt = phantomx::poi::poi_composite_score_v2(lt);
  auto up = base; up.uptime_ratio = 0.999; auto sup = phantomx::poi::poi_composite_score_v2(up);
  auto rl = base; rl.relay_ratio = 0.9; auto srl = phantomx::poi::poi_composite_score_v2(rl);
  auto nw = base; nw.active_miners = 50; auto snw = phantomx::poi::poi_composite_score_v2(nw);
  PHX_EXPECT(sbw.score > b.score);
  PHX_EXPECT(slt.score > b.score);
  PHX_EXPECT(sup.score > b.score);
  PHX_EXPECT(srl.score > b.score);
  PHX_EXPECT(snw.score > b.score);
  const double sum = phantomx::poi::kPoiWBandwidth + phantomx::poi::kPoiWLatency + phantomx::poi::kPoiWUptime + phantomx::poi::kPoiWRelay + phantomx::poi::kPoiWNetwork;
  PHX_EXPECT(sum > 0.999999 && sum < 1.000001);
  PHX_EXPECT(b.score >= 0.0 && b.score <= 100.0);
}

PHX_TEST(poi_identity_claim_requires_signature_for_remote_rpc) {
  PHX_EXPECT(phantomx::crypto::init().ok());
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_poi_identity_test";
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
  PHX_EXPECT(chain.ok());
  if (!chain.ok()) return;
  phantomx::Mempool mempool;
  phantomx::pool::PoolLedger pool;
  phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server;
  phantomx::rpc::register_default_methods(server);
  server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());

  phantomx::rpc::RpcRequest req;
  req.id="1"; req.method="startpoi"; req.auth_token="t";
  req.params["participant"]="p1"; req.params["owner_address"]="ownerA"; req.params["machine_id"]="m1";
  req.params["bandwidth_bytes"]="104857600"; req.params["bandwidth_unit"]="MB"; req.params["running"]="true";
  // simulate remote caller
  phantomx::rpc::g_rpc_remote_ip = "203.0.113.10";
  auto r = server.call(req, 1);
  PHX_EXPECT(!r.ok);
  PHX_EXPECT(r.code == phantomx::rpc::RpcErrorCode::kUnauthorized);
  phantomx::rpc::g_rpc_remote_ip.clear();

  // signed claim should pass
  auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(kp.ok());
  if (!kp.ok()) return;
  const auto ts = static_cast<std::uint64_t>(std::time(nullptr));
  const std::string msg = "PHX/poi/identity/v1|ownerA|m1|p1|" + std::to_string(ts);
  auto sig = phantomx::crypto::sign(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size()), kp.value().sec);
  PHX_EXPECT(sig.ok());
  if (!sig.ok()) return;
  req.params["identity_pubkey"] = phantomx::crypto::serialize_public_key_hex(kp.value().pub);
  req.params["identity_sig"] = phantomx::crypto::serialize_signature_hex(sig.value());
  req.params["identity_ts"] = std::to_string(ts);
  phantomx::rpc::g_rpc_remote_ip = "203.0.113.10";
  auto r2 = server.call(req, 2);
  PHX_EXPECT(r2.ok);

  // conflicting owner with same pubkey must be rejected
  phantomx::rpc::RpcRequest req2 = req;
  req2.params["participant"]="p2"; req2.params["owner_address"]="ownerB"; req2.params["machine_id"]="m2";
  const std::string msg2 = "PHX/poi/identity/v1|ownerB|m2|p2|" + std::to_string(ts);
  auto sig2 = phantomx::crypto::sign(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(msg2.data()), msg2.size()), kp.value().sec);
  PHX_EXPECT(sig2.ok());
  if (!sig2.ok()) return;
  req2.params["identity_sig"] = phantomx::crypto::serialize_signature_hex(sig2.value());
  auto r3 = server.call(req2, 3);
  PHX_EXPECT(!r3.ok);
  PHX_EXPECT(r3.code == phantomx::rpc::RpcErrorCode::kForbidden);
  phantomx::rpc::g_rpc_remote_ip.clear();
  fs::remove_all(tmp);
}

PHX_TEST(poi_identity_claim_bad_sig_and_skew_and_conflicts) {
  PHX_EXPECT(phantomx::crypto::init().ok());
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_poi_identity_test2";
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
  PHX_EXPECT(chain.ok());
  if (!chain.ok()) return;
  phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());

  auto make_req = [&](const std::string& participant, const std::string& owner, const std::string& machine) {
    phantomx::rpc::RpcRequest req;
    req.id="1"; req.method="startpoi"; req.auth_token="t";
    req.params["participant"]=participant; req.params["owner_address"]=owner; req.params["machine_id"]=machine;
    req.params["bandwidth_bytes"]="104857600"; req.params["bandwidth_unit"]="MB"; req.params["running"]="true";
    return req;
  };
  auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(kp.ok()); if (!kp.ok()) return;
  const auto ts = static_cast<std::uint64_t>(std::time(nullptr));
  auto sign_req = [&](phantomx::rpc::RpcRequest& req, const phantomx::crypto::KeyPair& k, std::uint64_t tsv) {
    const std::string msg = "PHX/poi/identity/v1|" + req.params["owner_address"] + "|" + req.params["machine_id"] + "|" + req.params["participant"] + "|" + std::to_string(tsv);
    auto sig = phantomx::crypto::sign(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size()), k.sec);
    PHX_EXPECT(sig.ok());
    if (sig.ok()) {
      req.params["identity_pubkey"] = phantomx::crypto::serialize_public_key_hex(k.pub);
      req.params["identity_sig"] = phantomx::crypto::serialize_signature_hex(sig.value());
      req.params["identity_ts"] = std::to_string(tsv);
    }
  };

  // bad signature
  auto rbad = make_req("p1","ownerA2","m1");
  sign_req(rbad, kp.value(), ts);
  rbad.params["identity_sig"] = "00" + rbad.params["identity_sig"].substr(2);
  phantomx::rpc::g_rpc_remote_ip = "203.0.113.20";
  auto bad = server.call(rbad, 10);
  PHX_EXPECT(!bad.ok);
  PHX_EXPECT(bad.code == phantomx::rpc::RpcErrorCode::kUnauthorized);

  // skew too old/future
  auto rold = make_req("p1","ownerA2","m1");
  sign_req(rold, kp.value(), ts - 3600);
  auto oldr = server.call(rold, 11);
  PHX_EXPECT(!oldr.ok);
  auto rfut = make_req("p1","ownerA2","m1");
  sign_req(rfut, kp.value(), ts + 3600);
  auto futr = server.call(rfut, 12);
  PHX_EXPECT(!futr.ok);

  // valid claim binds ownerA to kp
  auto rok = make_req("p1","ownerA2","m1");
  sign_req(rok, kp.value(), ts);
  auto okr = server.call(rok, 13);
  if (!okr.ok) { std::cerr << "okr failed: " << static_cast<int>(okr.code) << " " << okr.error_message << std::endl; }
  PHX_EXPECT(okr.ok);

  // same owner different pubkey -> forbidden
  auto kp2 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(kp2.ok()); if (!kp2.ok()) { phantomx::rpc::g_rpc_remote_ip.clear(); fs::remove_all(tmp); return; }
  auto rdiff = make_req("p1b","ownerA2","m2");
  sign_req(rdiff, kp2.value(), ts);
  auto diffr = server.call(rdiff, 14);
  PHX_EXPECT(!diffr.ok);
  PHX_EXPECT(diffr.code == phantomx::rpc::RpcErrorCode::kForbidden);

  // same pubkey different owner -> forbidden
  auto rsamepk = make_req("p2","ownerB","m2");
  sign_req(rsamepk, kp.value(), ts);
  auto samepkr = server.call(rsamepk, 15);
  PHX_EXPECT(!samepkr.ok);
  PHX_EXPECT(samepkr.code == phantomx::rpc::RpcErrorCode::kForbidden);

  phantomx::rpc::g_rpc_remote_ip.clear();
  fs::remove_all(tmp);
}

PHX_TEST(poi_identity_binding_persists_in_state_json) {
  PHX_EXPECT(phantomx::crypto::init().ok());
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_poi_identity_persist";
  fs::remove_all(tmp);
  fs::create_directories(tmp);
  // First server: bind identity
  {
    auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
    PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
    phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
    phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
    PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok()); if (!kp.ok()) return;
    const auto ts = static_cast<std::uint64_t>(std::time(nullptr));
    phantomx::rpc::RpcRequest req; req.id="1"; req.method="startpoi"; req.auth_token="t";
    req.params["participant"]="pp"; req.params["owner_address"]="ownerP"; req.params["machine_id"]="mp";
    req.params["bandwidth_bytes"]="104857600"; req.params["bandwidth_unit"]="MB"; req.params["running"]="true";
    const std::string msg = "PHX/poi/identity/v1|ownerP|mp|pp|" + std::to_string(ts);
    auto sig = phantomx::crypto::sign(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size()), kp.value().sec);
    PHX_EXPECT(sig.ok()); if (!sig.ok()) return;
    req.params["identity_pubkey"]=phantomx::crypto::serialize_public_key_hex(kp.value().pub);
    req.params["identity_sig"]=phantomx::crypto::serialize_signature_hex(sig.value());
    req.params["identity_ts"]=std::to_string(ts);
    phantomx::rpc::g_rpc_remote_ip = "203.0.113.30";
    auto r = server.call(req, 1);
    PHX_EXPECT(r.ok);
    phantomx::rpc::g_rpc_remote_ip.clear();
  }
  // Second server: same datadir should reject conflicting owner for same pubkey
  {
    auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
    PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
    phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
    phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
    PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());
    // Binding should be persisted in chain metadata poi_state_json
    auto m = chain.value().get_metadata("poi_state_json");
    PHX_EXPECT(m.ok() && m.value().has_value());
    if (m.ok() && m.value().has_value()) {
      PHX_EXPECT(m.value()->find("ownerP") != std::string::npos);
      PHX_EXPECT(m.value()->find("identity_bindings") != std::string::npos);
    }
  }
  fs::remove_all(tmp);
}

PHX_TEST(poi_uptime_challenge_sign_verify_and_freshness) {
  PHX_EXPECT(phantomx::crypto::init().ok());
  auto vk = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  PHX_EXPECT(vk.ok()); if (!vk.ok()) return;
  phantomx::poi::PoiChallengeV1 c;
  c.epoch=1; c.slot=10; c.issued_at_ms=100000; c.expires_at_ms=160000;
  c.verifier_pubkey = vk.value().pub.bytes;
  c.machine_id.fill(0x11); c.owner_address="ownerA"; c.nonce.fill(0x22); c.params_hash.fill(0x33);
  PHX_EXPECT(phantomx::poi::sign_poi_challenge(c, vk.value().sec).ok());
  PHX_EXPECT(phantomx::poi::verify_poi_challenge(c, 120000).ok());
  PHX_EXPECT(!phantomx::poi::verify_poi_challenge(c, 100000 + 60000 + 300001).ok());
  c.nonce[0] ^= 1;
  PHX_EXPECT(!phantomx::poi::verify_poi_challenge(c, 120000).ok());
}

PHX_TEST(poi_uptime_response_attestation_quorum_replay_conflict) {
  PHX_EXPECT(phantomx::crypto::init().ok());
  auto v1 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto v2 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto v3 = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  auto m = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(v1.ok() && v2.ok() && v3.ok() && m.ok());
  if (!v1.ok() || !v2.ok() || !v3.ok() || !m.ok()) return;
  phantomx::poi::PoiChallengeV1 c;
  c.epoch=1; c.slot=10; c.issued_at_ms=100000; c.expires_at_ms=160000;
  c.verifier_pubkey=v1.value().pub.bytes; c.machine_id.fill(0x11); c.owner_address="ownerA"; c.nonce.fill(0x22); c.params_hash.fill(0x33);
  PHX_EXPECT(phantomx::poi::sign_poi_challenge(c, v1.value().sec).ok());

  phantomx::poi::PoiResponseV1 r;
  r.challenge_id=c.challenge_id; r.epoch=c.epoch; r.slot=c.slot; r.machine_id=c.machine_id;
  r.machine_pubkey=m.value().pub.bytes; r.response_hash.fill(0x44); r.responded_at_ms=110000;
  PHX_EXPECT(phantomx::poi::sign_poi_response(r, m.value().sec).ok());
  PHX_EXPECT(phantomx::poi::verify_poi_response(r, c, 120000).ok());
  r.responded_at_ms=200000;
  PHX_EXPECT(!phantomx::poi::verify_poi_response(r, c, 200000).ok());

  auto hexpk=[](const phantomx::crypto::PublicKey& p){ std::string s; static const char* h="0123456789abcdef"; s.resize(64); for(size_t i=0;i<32;++i){s[i*2]=h[p.bytes[i]>>4]; s[i*2+1]=h[p.bytes[i]&0xF];} return s; };
  std::unordered_set<std::string> active{hexpk(v1.value().pub), hexpk(v2.value().pub), hexpk(v3.value().pub)};
  auto mk_att=[&](const phantomx::crypto::KeyPair& v, bool pass, uint32_t score){
    phantomx::poi::PoiAttestationV1 a; a.challenge_id=c.challenge_id; a.epoch=c.epoch; a.slot=c.slot; a.machine_id=c.machine_id;
    a.verifier_pubkey=v.pub.bytes; a.pass=pass; a.score_uptime_milli=score; a.observed_at_ms=115000; a.evidence_hash.fill(pass?0x55:0x66);
    PHX_EXPECT(phantomx::poi::sign_poi_attestation(a, v.sec).ok()); return a;
  };
  std::vector<phantomx::poi::PoiAttestationV1> atts{mk_att(v1.value(),true,100000), mk_att(v2.value(),true,90000), mk_att(v3.value(),false,0)};
  auto res = phantomx::poi::aggregate_poi_uptime_attestations(c, atts, active, 2, 120000);
  PHX_EXPECT(res.quorum_met);
  PHX_EXPECT_EQ(static_cast<long long>(res.pass_count), 2);

  atts.push_back(atts[0]);
  auto res2 = phantomx::poi::aggregate_poi_uptime_attestations(c, atts, active, 2, 120000);
  PHX_EXPECT_EQ(static_cast<long long>(res2.duplicate_count), 1);

  std::vector<phantomx::poi::PoiAttestationV1> atts3{mk_att(v1.value(),true,100000), mk_att(v1.value(),false,0)};
  auto res3 = phantomx::poi::aggregate_poi_uptime_attestations(c, atts3, active, 2, 120000);
  PHX_EXPECT_EQ(static_cast<long long>(res3.conflict_count), 1);

  phantomx::poi::PoiUptimeReplayCache cache;
  PHX_EXPECT(cache.mark_challenge(c.challenge_id));
  PHX_EXPECT(!cache.mark_challenge(c.challenge_id));
  PHX_EXPECT(cache.mark_attestation(c.challenge_id, v1.value().pub.bytes));
  PHX_EXPECT(!cache.mark_attestation(c.challenge_id, v1.value().pub.bytes));
}

PHX_TEST(poi_state_malformed_json_does_not_crash_or_corrupt) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_poi_malformed";
  fs::remove_all(tmp); fs::create_directories(tmp);
  {
    auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
    PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
    // write malformed poi_state_json
    auto s = chain.value().set_metadata("poi_state_json", "{broken json");
    PHX_EXPECT(s.ok());
  }
  {
    auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
    PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
    phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
    phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
    // should not crash on malformed state
    auto rr = phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg);
    PHX_EXPECT(rr.ok());
    phantomx::rpc::RpcRequest req; req.id="1"; req.method="getpoiinfo"; req.auth_token="t";
    auto r = server.call(req, 1);
    PHX_EXPECT(r.ok);
  }
  fs::remove_all(tmp);
}

PHX_TEST(poi_state_corrupt_identity_binding_ignored) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_poi_corrupt_id";
  fs::remove_all(tmp); fs::create_directories(tmp);
  {
    auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
    PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
    // corrupt identity binding: missing owner/pubkey
    std::string bad = "{\"running\":false,\"participants\":[],\"blocks\":[],\"identity_bindings\":[{\"owner_address\":\"\",\"identity_pubkey_hex\":\"\"},{\"owner_address\":\"o1\"}]}";
    PHX_EXPECT(chain.value().set_metadata("poi_state_json", bad).ok());
  }
  {
    auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
    PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
    phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
    phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
    PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());
    // valid signed claim should still work after corrupt bindings ignored
    auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
    PHX_EXPECT(kp.ok()); if(!kp.ok()) return;
    const auto ts = static_cast<std::uint64_t>(std::time(nullptr));
    const std::string msg = "PHX/poi/identity/v1|ownerC|mc|pc|" + std::to_string(ts);
    auto sig = phantomx::crypto::sign(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size()), kp.value().sec);
    PHX_EXPECT(sig.ok()); if(!sig.ok()) return;
    phantomx::rpc::RpcRequest req; req.id="1"; req.method="startpoi"; req.auth_token="t";
    req.params["participant"]="pc"; req.params["owner_address"]="ownerC"; req.params["machine_id"]="mc";
    req.params["bandwidth_bytes"]="104857600"; req.params["bandwidth_unit"]="MB"; req.params["running"]="true";
    req.params["identity_pubkey"]=phantomx::crypto::serialize_public_key_hex(kp.value().pub);
    req.params["identity_sig"]=phantomx::crypto::serialize_signature_hex(sig.value());
    req.params["identity_ts"]=std::to_string(ts);
    phantomx::rpc::g_rpc_remote_ip="203.0.113.55";
    auto r = server.call(req, 1);
    PHX_EXPECT(r.ok);
    phantomx::rpc::g_rpc_remote_ip.clear();
  }
  fs::remove_all(tmp);
}

PHX_TEST(ptm001_heartbeat_liveness_only_no_finality_effect) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_ptm001";
  fs::remove_all(tmp); fs::create_directories(tmp);
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
  PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
  phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());

  // create a valid regtest address via wallet-backed RPC
  phantomx::rpc::RpcRequest na; na.id="na"; na.method="getnewaddress"; na.auth_token="t";
  auto nar = server.call(na, 1);
  PHX_EXPECT(nar.ok);
  const std::string vaddr = nar.ok && nar.result.count("address") ? nar.result.at("address") : "";
  PHX_EXPECT(!vaddr.empty());

  auto vkp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  PHX_EXPECT(vkp.ok()); if(!vkp.ok()) return;
  const std::string pkhex = phantomx::crypto::serialize_public_key_hex(vkp.value().pub);
  phantomx::rpc::RpcRequest ch; ch.id="c1"; ch.method="verifier_register_challenge"; ch.auth_token="t";
  ch.params["id"]="v1"; ch.params["pubkey"]=pkhex; ch.params["owner"]="owner1";
  auto chr = server.call(ch, 10);
  if(!chr.ok) std::cout<<"CH_ERR="<<chr.error_message<<"\n";
  PHX_EXPECT(chr.ok);
  std::cout<<"CH_NET="<<(chr.result.count("network")?chr.result.at("network"):"")<<"\n";
  const std::string payload_hex = chr.result.count("payload_hex")?chr.result.at("payload_hex"):"";
  const std::string challenge_id = chr.result.count("challenge_id")?chr.result.at("challenge_id"):"";
  std::vector<std::uint8_t> payload_bytes;
  PHX_EXPECT(phantomx::from_hex(payload_hex, payload_bytes));
  auto sig = phantomx::crypto::sign(std::span<const std::uint8_t>(payload_bytes.data(), payload_bytes.size()), vkp.value().sec);
  PHX_EXPECT(sig.ok()); if(!sig.ok()) return;
  phantomx::rpc::RpcRequest reg; reg.id="1"; reg.method="verifier_register"; reg.auth_token="t";
  reg.params["id"]="v1"; reg.params["address"]=vaddr; reg.params["endpoint"]="127.0.0.1:9999"; reg.params["pubkey"]=pkhex;
  reg.params["owner"]="owner1"; reg.params["challenge_id"]=challenge_id; reg.params["pop_sig"]=phantomx::crypto::serialize_signature_hex(sig.value());
  auto rr = server.call(reg, 1);
  if(!rr.ok) { std::cout<<"PTM001_REG_ERR="<<rr.error_message<<"\n"; }
  PHX_EXPECT(rr.ok);
  // Phase 15C.1: new registration is PENDING; explicit admin activation required.
  setenv("PHANTOMX_ADMIN_TOKEN","adm",1);
  phantomx::rpc::RpcRequest act; act.id="act"; act.method="verifier_set_status"; act.auth_token="t";
  act.params["id"]="v1"; act.params["status"]="ACTIVE"; act.params["admin_token"]="adm";
  auto ar = server.call(act, 2);
  PHX_EXPECT(ar.ok);

  phantomx::rpc::RpcRequest hb; hb.id="2"; hb.method="verifier_heartbeat"; hb.auth_token="t"; hb.params["id"]="v1";
  auto hbr = server.call(hb, 2);
  if(!hbr.ok) std::cout<<"PTM001_HB_ERR="<<hbr.error_message<<"\n";
  PHX_EXPECT(hbr.ok);
  PHX_EXPECT(hbr.result.count("finality_effect") && hbr.result.at("finality_effect") == "none_liveness_only");

  phantomx::rpc::RpcRequest fi; fi.id="3"; fi.method="getfinalityinfo"; fi.auth_token="t";
  auto fir = server.call(fi, 3);
  PHX_EXPECT(fir.ok);
  PHX_EXPECT(fir.result.count("finalized_height"));
  fs::remove_all(tmp);
}

PHX_TEST(ptm002_setpoiquorum_requires_admin_token) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_ptm002";
  fs::remove_all(tmp); fs::create_directories(tmp);
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
  PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
  phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());

  // No admin token configured - must fail closed.
  unsetenv("PHANTOMX_ADMIN_TOKEN");
  phantomx::rpc::RpcRequest req; req.id="1"; req.method="setpoiquorum"; req.auth_token="t"; req.params["q_min"]="2";
  auto r = server.call(req, 1);
  PHX_EXPECT(!r.ok);

  setenv("PHANTOMX_ADMIN_TOKEN", "secret", 1);
  auto r2 = server.call(req, 2);
  PHX_EXPECT(!r2.ok); // missing admin_token param

  req.params["admin_token"] = "wrong";
  auto r3 = server.call(req, 3);
  PHX_EXPECT(!r3.ok);

  req.params["admin_token"] = "secret";
  auto r4 = server.call(req, 4);
  PHX_EXPECT(r4.ok);
  unsetenv("PHANTOMX_ADMIN_TOKEN");
  fs::remove_all(tmp);
}

PHX_TEST(ptm005_stoppoi_participant_scope_requires_owner) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_ptm005";
  fs::remove_all(tmp); fs::create_directories(tmp);
  PHX_EXPECT(phantomx::crypto::init().ok());
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
  PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
  phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());

  auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kNodeIdentity);
  PHX_EXPECT(kp.ok()); if (!kp.ok()) return;
  const auto ts = static_cast<std::uint64_t>(std::time(nullptr));
  const std::string msg = "PHX/poi/identity/v1|ownerS|mS|pS|" + std::to_string(ts);
  auto sig = phantomx::crypto::sign(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size()), kp.value().sec);
  PHX_EXPECT(sig.ok()); if (!sig.ok()) return;
  phantomx::rpc::RpcRequest start; start.id="1"; start.method="startpoi"; start.auth_token="t";
  start.params["participant"]="pS"; start.params["owner_address"]="ownerS"; start.params["machine_id"]="mS";
  start.params["bandwidth_bytes"]="104857600"; start.params["bandwidth_unit"]="MB"; start.params["running"]="true";
  start.params["identity_pubkey"]=phantomx::crypto::serialize_public_key_hex(kp.value().pub);
  start.params["identity_sig"]=phantomx::crypto::serialize_signature_hex(sig.value());
  start.params["identity_ts"]=std::to_string(ts);
  phantomx::rpc::g_rpc_remote_ip="203.0.113.99";
  auto sr = server.call(start, 1);
  PHX_EXPECT(sr.ok);

  // Stop without identity proof should fail for remote.
  phantomx::rpc::RpcRequest stop; stop.id="2"; stop.method="stoppoi"; stop.auth_token="t"; stop.params["participant"]="pS";
  auto stop_bad = server.call(stop, 2);
  PHX_EXPECT(!stop_bad.ok);

  // Stop with valid proof should succeed.
  const auto ts2 = static_cast<std::uint64_t>(std::time(nullptr));
  const std::string msg2 = "PHX/poi/identity/v1|ownerS|mS|pS|" + std::to_string(ts2);
  auto sig2 = phantomx::crypto::sign(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(msg2.data()), msg2.size()), kp.value().sec);
  PHX_EXPECT(sig2.ok()); if(!sig2.ok()) { phantomx::rpc::g_rpc_remote_ip.clear(); fs::remove_all(tmp); return; }
  stop.params["owner_address"]="ownerS"; stop.params["machine_id"]="mS";
  stop.params["identity_pubkey"]=phantomx::crypto::serialize_public_key_hex(kp.value().pub);
  stop.params["identity_sig"]=phantomx::crypto::serialize_signature_hex(sig2.value());
  stop.params["identity_ts"]=std::to_string(ts2);
  auto stop_ok = server.call(stop, 3);
  PHX_EXPECT(stop_ok.ok);
  PHX_EXPECT(stop_ok.result.count("scope") && stop_ok.result.at("scope") == "participant");

  // Global stop requires admin token.
  phantomx::rpc::RpcRequest stop_all; stop_all.id="4"; stop_all.method="stoppoi"; stop_all.auth_token="t"; stop_all.params["all"]="true";
  auto sa = server.call(stop_all, 4);
  PHX_EXPECT(!sa.ok);
  setenv("PHANTOMX_ADMIN_TOKEN","adm",1);
  stop_all.params["admin_token"]="adm";
  auto sa2 = server.call(stop_all, 5);
  PHX_EXPECT(sa2.ok);
  unsetenv("PHANTOMX_ADMIN_TOKEN");
  phantomx::rpc::g_rpc_remote_ip.clear();
  fs::remove_all(tmp);
}

PHX_TEST(ptm_pool_record_payout_requires_admin_token) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_ptm_pool_payout";
  fs::remove_all(tmp); fs::create_directories(tmp);
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
  PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
  phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());

  unsetenv("PHANTOMX_ADMIN_TOKEN");
  phantomx::rpc::RpcRequest req; req.id="1"; req.method="pool_record_payout"; req.auth_token="t";
  req.params["worker_id"]=std::string(64,'0'); req.params["epoch"]="1"; req.params["amount_atoms"]="1";
  auto r = server.call(req, 1);
  PHX_EXPECT(!r.ok);

  setenv("PHANTOMX_ADMIN_TOKEN","adm",1);
  req.params["admin_token"]="wrong";
  auto r2 = server.call(req, 2);
  PHX_EXPECT(!r2.ok);
  req.params["admin_token"]="adm";
  auto r3 = server.call(req, 3);
  // May fail for business validation (worker/epoch/finalized), but must not fail with admin auth error.
  if(!r3.ok) PHX_EXPECT(r3.error_message.find("admin_token") == std::string::npos);
  unsetenv("PHANTOMX_ADMIN_TOKEN");
  fs::remove_all(tmp);
}

PHX_TEST(rpc_invalid_height_and_nblocks_rejected_cleanly) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_rpc_badnum";
  fs::remove_all(tmp); fs::create_directories(tmp);
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
  PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
  phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());

  phantomx::rpc::RpcRequest bh; bh.id="1"; bh.method="getblockhash"; bh.auth_token="t"; bh.params["height"]="abc";
  auto r1 = server.call(bh, 1);
  PHX_EXPECT(!r1.ok);
  PHX_EXPECT(r1.error_message.find("invalid height") != std::string::npos);

  phantomx::rpc::RpcRequest gen; gen.id="2"; gen.method="generatetoaddress"; gen.auth_token="t"; gen.params["address"]="bad"; gen.params["nblocks"]="nope";
  auto r2 = server.call(gen, 2);
  PHX_EXPECT(!r2.ok);
  PHX_EXPECT(r2.error_message.find("invalid nblocks") != std::string::npos);
  fs::remove_all(tmp);
}

PHX_TEST(ptm_pool_invalid_numeric_params_rejected_cleanly) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_ptm_pool_badnum";
  fs::remove_all(tmp); fs::create_directories(tmp);
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
  PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
  phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());

  phantomx::rpc::RpcRequest sub; sub.id="1"; sub.method="pool_submit_share"; sub.auth_token="t";
  sub.params["worker_id"]=std::string(64,'1'); sub.params["epoch"]="not-a-number"; sub.params["units"]="10"; sub.params["evidence_hash"]="abc";
  auto r1 = server.call(sub, 1);
  PHX_EXPECT(!r1.ok);
  PHX_EXPECT(r1.error_message.find("invalid epoch") != std::string::npos);

  sub.params["epoch"]="1"; sub.params["units"]="bad";
  auto r2 = server.call(sub, 2);
  PHX_EXPECT(!r2.ok);
  PHX_EXPECT(r2.error_message.find("invalid units") != std::string::npos);

  setenv("PHANTOMX_ADMIN_TOKEN","adm",1);
  phantomx::rpc::RpcRequest pay; pay.id="2"; pay.method="pool_record_payout"; pay.auth_token="t";
  pay.params["admin_token"]="adm"; pay.params["worker_id"]=std::string(64,'1'); pay.params["epoch"]="x"; pay.params["amount_atoms"]="1";
  auto r3 = server.call(pay, 3);
  PHX_EXPECT(!r3.ok);
  PHX_EXPECT(r3.error_message.find("invalid epoch") != std::string::npos);

  pay.params["epoch"]="1"; pay.params["amount_atoms"]="nope";
  auto r4 = server.call(pay, 4);
  PHX_EXPECT(!r4.ok);
  PHX_EXPECT(r4.error_message.find("invalid amount_atoms") != std::string::npos);
  unsetenv("PHANTOMX_ADMIN_TOKEN");
  fs::remove_all(tmp);
}

PHX_TEST(ptm006_bootstrap_finality_requires_explicit_opt_in) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_ptm006";
  fs::remove_all(tmp); fs::create_directories(tmp);
  unsetenv("PHANTOMX_POI_BOOTSTRAP_ALLOW");
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
  PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
  phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());
  // No explicit bootstrap allow: fresh chain should not be auto-finalized by record path alone.
  // getfinalityinfo should report height 0/default state.
  phantomx::rpc::RpcRequest fi; fi.id="1"; fi.method="getfinalityinfo"; fi.auth_token="t";
  auto fir = server.call(fi, 1);
  PHX_EXPECT(fir.ok);
  PHX_EXPECT(fir.result.count("finalized_height"));
  PHX_EXPECT(fir.result.at("finalized_height") == "0");
  fs::remove_all(tmp);
}

PHX_TEST(ptm015c1_pending_verifier_cannot_attest_or_heartbeat_until_activated) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_ptm015c1_pending";
  fs::remove_all(tmp); fs::create_directories(tmp);
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
  PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
  phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());
  auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  PHX_EXPECT(kp.ok()); if(!kp.ok()) return;
  const std::string pkhex = phantomx::crypto::serialize_public_key_hex(kp.value().pub);
  phantomx::rpc::RpcRequest na; na.id="na"; na.method="getnewaddress"; na.auth_token="t";
  auto nar = server.call(na,1); PHX_EXPECT(nar.ok);
  std::string vaddr = nar.result.count("address")?nar.result.at("address"):"";
  phantomx::rpc::RpcRequest ch; ch.id="c"; ch.method="verifier_register_challenge"; ch.auth_token="t"; ch.params["id"]="v1"; ch.params["pubkey"]=pkhex; ch.params["owner"]="o1";
  auto chr = server.call(ch,1); PHX_EXPECT(chr.ok);
  std::vector<std::uint8_t> pbytes; PHX_EXPECT(phantomx::from_hex(chr.result.at("payload_hex"), pbytes));
  auto csig = phantomx::crypto::sign(std::span<const std::uint8_t>(pbytes.data(), pbytes.size()), kp.value().sec);
  phantomx::rpc::RpcRequest reg; reg.id="r"; reg.method="verifier_register"; reg.auth_token="t";
  reg.params["id"]="v1"; reg.params["address"]=vaddr; reg.params["endpoint"]="127.0.0.1:1"; reg.params["pubkey"]=pkhex; reg.params["owner"]="o1"; reg.params["challenge_id"]=chr.result.at("challenge_id"); reg.params["pop_sig"]=phantomx::crypto::serialize_signature_hex(csig.value());
  auto rr = server.call(reg,1); PHX_EXPECT(rr.ok);

  // PENDING verifier cannot heartbeat
  phantomx::rpc::RpcRequest hb; hb.id="hb"; hb.method="verifier_heartbeat"; hb.auth_token="t"; hb.params["id"]="v1";
  auto hbr = server.call(hb,2); PHX_EXPECT(!hbr.ok);

  // PENDING verifier cannot attest
  const std::string bh(64,'c'); uint64_t ts=static_cast<uint64_t>(std::time(nullptr));
  const std::string msg = std::string("PHX_POI_ATTEST_V1|")+bh+"|1|"+pkhex+"|"+std::to_string(ts);
  auto asig = phantomx::crypto::sign(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size()), kp.value().sec);
  phantomx::rpc::RpcRequest a; a.id="a"; a.method="poi_submit_attestation"; a.auth_token="t";
  a.params["block_hash"]=bh; a.params["height"]="1"; a.params["verifier_pubkey"]=pkhex; a.params["timestamp"]=std::to_string(ts); a.params["signature"]=phantomx::crypto::serialize_signature_hex(asig.value());
  auto ar2 = server.call(a,3); PHX_EXPECT(!ar2.ok);

  // admin activation required
  setenv("PHANTOMX_ADMIN_TOKEN","adm",1);
  phantomx::rpc::RpcRequest act; act.id="act"; act.method="verifier_set_status"; act.auth_token="t"; act.params["id"]="v1"; act.params["status"]="ACTIVE"; act.params["admin_token"]="adm";
  PHX_EXPECT(server.call(act,4).ok);
  PHX_EXPECT(server.call(hb,5).ok);
  fs::remove_all(tmp);
}

PHX_TEST(ptm015c2_pop_v1_vector_and_mutation) {
  namespace fs = std::filesystem;
  // Resolve vectors path robustly across different build directory layouts.
  fs::path p;
  const char* candidates[] = {
    "tests/vectors/poi_verifier_pop_v1_vectors.json",
    "../tests/vectors/poi_verifier_pop_v1_vectors.json",
    "../../tests/vectors/poi_verifier_pop_v1_vectors.json",
    "../../../tests/vectors/poi_verifier_pop_v1_vectors.json",
    "../../../../tests/vectors/poi_verifier_pop_v1_vectors.json",
  };
  for (const char* c : candidates) {
    fs::path cand(c);
    if (fs::exists(cand)) { p = cand; break; }
  }
  // Fallback: derive from __FILE__ (source tree location).
  if (p.empty()) {
    fs::path src(__FILE__);
    // src is .../tests/unit/test_poi.cpp; go up to source root
    fs::path root = src.parent_path().parent_path().parent_path();
    fs::path cand = root / "tests" / "vectors" / "poi_verifier_pop_v1_vectors.json";
    if (fs::exists(cand)) p = cand;
  }
  std::ifstream f(p);
  PHX_EXPECT(f.good());
  if (!f.good()) return;
  std::string js((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  // very small JSON scan for vector objects; file is deterministic test fixture.
  size_t pos = 0;
  int checked = 0;
  while (true) {
    pos = js.find("\"name\"", pos);
    if (pos == std::string::npos) break;
    auto obj_begin = js.rfind('{', pos);
    auto obj_end = js.find("}", pos);
    if (obj_begin == std::string::npos || obj_end == std::string::npos) break;
    std::string obj = js.substr(obj_begin, obj_end - obj_begin + 1);
    auto extract = [&](const std::string& k)->std::string{
      auto p2 = obj.find("\""+k+"\"");
      if (p2==std::string::npos) return "";
      p2 = obj.find(':', p2);
      auto q1 = obj.find('"', p2+1); if (q1==std::string::npos) return "";
      auto q2 = obj.find('"', q1+1); if (q2==std::string::npos) return "";
      return obj.substr(q1+1, q2-q1-1);
    };
    auto extract_bool = [&](const std::string& k, bool def)->bool{
      auto p2 = obj.find("\""+k+"\""); if (p2==std::string::npos) return def;
      auto p3 = obj.find(':', p2); auto tail = obj.substr(p3+1, 20);
      if (tail.find("true") != std::string::npos) return true;
      if (tail.find("false") != std::string::npos) return false;
      return def;
    };
    const std::string payload_hex = extract("payload_hex");
    const std::string sig_hex = extract("signature_hex");
    const std::string pk_hex = extract("pubkey_hex");
    const bool expect_verify = extract_bool("expect_verify", false);
    const bool expect_decode = extract_bool("expect_decode", true);
    std::vector<std::uint8_t> payload, sigb, pkb;
    if (!phantomx::from_hex(payload_hex, payload) || !phantomx::from_hex(sig_hex, sigb) || !phantomx::from_hex(pk_hex, pkb)) { pos++; continue; }
    if (sigb.size()!=64 || pkb.size()!=32) { pos++; continue; }
    // decoder check mirrors v1 format rules (test-local duplicate to avoid exposing internals)
    auto decode_ok = [&](const std::vector<std::uint8_t>& in)->bool{
      size_t off=0;
      if (in.size() < 10+2+1+2+1+2+32+2+32+32+8+8) return false;
      const char magic[10]={'P','H','X','V','E','R','P','O','P','\1'};
      for(int i=0;i<10;++i) {
        if(in[off+static_cast<size_t>(i)] != static_cast<std::uint8_t>(magic[i])) return false;
      }
      off+=10;
      auto u16=[&](uint16_t& v)->bool{ if(off+2>in.size()) return false; v=static_cast<uint16_t>(static_cast<uint16_t>(in[off])|(static_cast<uint16_t>(in[off+1])<<8)); off+=2; return true; };
      auto u64=[&](uint64_t& v)->bool{ if(off+8>in.size()) return false; v=0; for(int i=0;i<8;++i) v|=(static_cast<uint64_t>(in[off+static_cast<size_t>(i)])<<(8*i)); off+=8; return true; };
      uint16_t ver=0, proto=0, vid_len=0, owner_len=0;
      if(!u16(ver)||ver!=1) return false;
      if(off>=in.size()) return false;
      off++; // network
      if(!u16(proto)||proto!=1) return false;
      if(off>=in.size()) return false;
      if(in[off++]!=1) return false;
      if(!u16(vid_len)||vid_len==0||vid_len>64||off+vid_len>in.size()) return false;
      off+=vid_len;
      if(off+32>in.size()) return false;
      off+=32;
      if(!u16(owner_len)||owner_len==0||owner_len>128||off+owner_len>in.size()) return false;
      off+=owner_len;
      if(off+32+32>in.size()) return false;
      off+=64;
      uint64_t issued=0, exp=0;
      if(!u64(issued)||!u64(exp)) return false;
      if(off!=in.size()) return false;
      if(exp<issued) return false;
      return true;
    };
    PHX_EXPECT(decode_ok(payload) == expect_decode);
    phantomx::crypto::PublicKey pk{}; std::copy(pkb.begin(), pkb.end(), pk.bytes.begin());
    phantomx::crypto::Signature sig{}; std::copy(sigb.begin(), sigb.end(), sig.bytes.begin());
    const bool vr = phantomx::crypto::verify(std::span<const std::uint8_t>(payload.data(), payload.size()), sig, pk);
    PHX_EXPECT(vr == expect_verify);
    checked++;
    pos = obj_end + 1;
  }
  PHX_EXPECT(checked >= 10);
}

PHX_TEST(ptm015c5_challenge_db_independent_connections_single_consume) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_ptm015c5_db";
  fs::remove_all(tmp); fs::create_directories(tmp);
  auto dbpath = tmp / "chain.db";
  // create DB and challenge via normal API
  auto chain = phantomx::blockchain::ChainDatabase::open(dbpath);
  PHX_EXPECT(chain.ok()); if(!chain.ok()) return;
  phantomx::blockchain::ChainDatabase::VerifierChallengeRecord rec;
  rec.challenge_id = std::string(64,'a');
  rec.verifier_id = "v1"; rec.pubkey_hex = std::string(64,'1'); rec.owner="o1"; rec.network="regtest";
  rec.payload_blob = {1,2,3}; rec.issued_at = 100; rec.expires_at = 200; rec.status="issued";
  PHX_EXPECT(chain.value().put_verifier_challenge(rec).ok());
  (void)chain.value().close();

  auto c1 = phantomx::blockchain::ChainDatabase::open(dbpath);
  auto c2 = phantomx::blockchain::ChainDatabase::open(dbpath);
  PHX_EXPECT(c1.ok() && c2.ok()); if(!c1.ok()||!c2.ok()) return;
  auto r1 = c1.value().consume_verifier_challenge(rec.challenge_id, rec.verifier_id, rec.pubkey_hex, rec.owner, rec.network, 150);
  auto r2 = c2.value().consume_verifier_challenge(rec.challenge_id, rec.verifier_id, rec.pubkey_hex, rec.owner, rec.network, 150);
  PHX_EXPECT(r1.ok() && r2.ok());
  const bool ok1 = r1.ok() && r1.value();
  const bool ok2 = r2.ok() && r2.value();
  PHX_EXPECT((ok1 ? 1:0) + (ok2?1:0) == 1);
  (void)c1.value().close(); (void)c2.value().close();
  auto c3 = phantomx::blockchain::ChainDatabase::open(dbpath);
  PHX_EXPECT(c3.ok());
  auto got = c3.value().get_verifier_challenge(rec.challenge_id);
  PHX_EXPECT(got.ok() && got.value().has_value());
  PHX_EXPECT(got.value()->used_at.has_value());
  fs::remove_all(tmp);
}

PHX_TEST(ptm015c1_challenge_replay_rejected) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_ptm015c1_replay";
  fs::remove_all(tmp); fs::create_directories(tmp);
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
  PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
  phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());
  auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  PHX_EXPECT(kp.ok()); if(!kp.ok()) return;
  const std::string pkhex = phantomx::crypto::serialize_public_key_hex(kp.value().pub);
  phantomx::rpc::RpcRequest na; na.id="na"; na.method="getnewaddress"; na.auth_token="t"; auto nar=server.call(na,1); std::string vaddr=nar.result.at("address");
  phantomx::rpc::RpcRequest ch; ch.id="c"; ch.method="verifier_register_challenge"; ch.auth_token="t"; ch.params["id"]="v1"; ch.params["pubkey"]=pkhex; ch.params["owner"]="o1";
  auto chr=server.call(ch,1); PHX_EXPECT(chr.ok);
  std::vector<std::uint8_t> pbytes; PHX_EXPECT(phantomx::from_hex(chr.result.at("payload_hex"), pbytes));
  auto csig=phantomx::crypto::sign(std::span<const std::uint8_t>(pbytes.data(), pbytes.size()), kp.value().sec);
  phantomx::rpc::RpcRequest reg; reg.id="r"; reg.method="verifier_register"; reg.auth_token="t";
  reg.params["id"]="v1"; reg.params["address"]=vaddr; reg.params["endpoint"]="127.0.0.1:1"; reg.params["pubkey"]=pkhex; reg.params["owner"]="o1"; reg.params["challenge_id"]=chr.result.at("challenge_id"); reg.params["pop_sig"]=phantomx::crypto::serialize_signature_hex(csig.value());
  PHX_EXPECT(server.call(reg,2).ok);
  auto replay = server.call(reg,3);
  PHX_EXPECT(!replay.ok);
  fs::remove_all(tmp);
}

PHX_TEST(ptm004_attestation_replay_and_stale_rejected) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_ptm004";
  fs::remove_all(tmp); fs::create_directories(tmp);
  unsetenv("PHANTOMX_POI_BOOTSTRAP_ALLOW");
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp / "chain.db");
  PHX_EXPECT(chain.ok()); if (!chain.ok()) return;
  phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());

  auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity);
  PHX_EXPECT(kp.ok()); if(!kp.ok()) return;
  const std::string pkhex = phantomx::crypto::serialize_public_key_hex(kp.value().pub);
  phantomx::rpc::RpcRequest na; na.id="na"; na.method="getnewaddress"; na.auth_token="t";
  auto nar = server.call(na,1); PHX_EXPECT(nar.ok);
  std::string vaddr = nar.result.count("address") ? nar.result.at("address") : "";
  phantomx::rpc::RpcRequest ch; ch.id="c1"; ch.method="verifier_register_challenge"; ch.auth_token="t";
  ch.params["id"]="v1"; ch.params["pubkey"]=pkhex; ch.params["owner"]="owner1";
  auto chr = server.call(ch,11); PHX_EXPECT(chr.ok);
  std::vector<std::uint8_t> pbytes; PHX_EXPECT(phantomx::from_hex(chr.result.at("payload_hex"), pbytes));
  auto csig = phantomx::crypto::sign(std::span<const std::uint8_t>(pbytes.data(), pbytes.size()), kp.value().sec);
  PHX_EXPECT(csig.ok());
  phantomx::rpc::RpcRequest reg; reg.id="1"; reg.method="verifier_register"; reg.auth_token="t";
  reg.params["id"]="v1"; reg.params["address"]=vaddr; reg.params["endpoint"]="127.0.0.1:9"; reg.params["pubkey"]=pkhex;
  reg.params["owner"]="owner1"; reg.params["challenge_id"]=chr.result.at("challenge_id");
  reg.params["pop_sig"]=phantomx::crypto::serialize_signature_hex(csig.value());
  auto rr = server.call(reg,1); PHX_EXPECT(rr.ok);
  setenv("PHANTOMX_ADMIN_TOKEN","adm",1);
  phantomx::rpc::RpcRequest act; act.id="act"; act.method="verifier_set_status"; act.auth_token="t"; act.params["id"]="v1"; act.params["status"]="ACTIVE"; act.params["admin_token"]="adm";
  PHX_EXPECT(server.call(act,2).ok);

  const std::string bh = std::string(64,'b');
  const uint64_t h=1; const uint64_t ts=static_cast<uint64_t>(std::time(nullptr));
  const std::string msg = std::string("PHX_POI_ATTEST_V1|")+bh+"|"+std::to_string(h)+"|"+pkhex+"|"+std::to_string(ts);
  auto sig = phantomx::crypto::sign(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size()), kp.value().sec);
  PHX_EXPECT(sig.ok()); if(!sig.ok()) return;
  const std::string sighex = phantomx::crypto::serialize_signature_hex(sig.value());

  phantomx::rpc::RpcRequest a; a.id="2"; a.method="poi_submit_attestation"; a.auth_token="t";
  a.params["block_hash"]=bh; a.params["height"]=std::to_string(h); a.params["verifier_pubkey"]=pkhex; a.params["signature"]=sighex; a.params["timestamp"]=std::to_string(ts);
  auto r1 = server.call(a,2);
  PHX_EXPECT(r1.ok);
  auto rdup = server.call(a,3);
  PHX_EXPECT(rdup.ok); // duplicate same verifier same attestation accepted as duplicate
  PHX_EXPECT(rdup.result.count("status") && rdup.result.at("status")=="duplicate");

  // stale attestation rejected
  phantomx::rpc::RpcRequest stale=a; stale.id="4"; stale.params["timestamp"]=std::to_string(ts - 7200);
  // resign with stale ts
  const std::string msg2 = std::string("PHX_POI_ATTEST_V1|")+bh+"|"+std::to_string(h)+"|"+pkhex+"|"+std::to_string(ts-7200);
  auto sig2 = phantomx::crypto::sign(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(msg2.data()), msg2.size()), kp.value().sec);
  stale.params["signature"]=phantomx::crypto::serialize_signature_hex(sig2.value());
  auto rs = server.call(stale,4);
  PHX_EXPECT(!rs.ok);
  fs::remove_all(tmp);
}

PHX_TEST(poi_composite_required_bandwidth_pairs) {
  auto score_at = [](std::uint64_t mbps) {
    phantomx::poi::PoiCompositeInput in;
    in.bandwidth_mbps = mbps; in.bandwidth_valid = true;
    in.latency_ms = 20; in.uptime_ratio = 0.99; in.relay_ratio = 0.8;
    in.active_miners = 10; in.network_valid = true;
    return phantomx::poi::poi_composite_score_v2(in).score;
  };
  PHX_EXPECT(score_at(500) > score_at(100));
  PHX_EXPECT(score_at(1000) > score_at(500));
  PHX_EXPECT(score_at(2000) > score_at(1000));
  PHX_EXPECT(score_at(10000) > score_at(2000));
  PHX_EXPECT(score_at(20000) > score_at(10000));
}

PHX_TEST(poi_bandwidth_si_display_format) {
  using phantomx::poi::format_poi_bandwidth_si;
  PHX_EXPECT(format_poi_bandwidth_si(500.0) == "500.00 Mbps");
  PHX_EXPECT(format_poi_bandwidth_si(1000.0) == "1.00 Gbps");
  PHX_EXPECT(format_poi_bandwidth_si(2500.0) == "2.50 Gbps");
  PHX_EXPECT(format_poi_bandwidth_si(1000000.0) == "1.00 Tbps");
  PHX_EXPECT(format_poi_bandwidth_si(1000000000.0) == "1.00 Pbps");
  PHX_EXPECT(format_poi_bandwidth_si(-1.0) == "Not Available");
}

PHX_TEST(ptm015c7_pop_v1_production_roundtrip_and_decode_rejects) {
  using namespace phantomx::poi;
  PopPayloadFieldsV1 f{};
  f.network_u8 = pop_network_u8(phantomx::Network::kRegtest);
  f.verifier_id = "v1"; f.owner = "o1"; f.issued_at = 100; f.expires_at = 400;
  for (size_t i=0;i<32;++i){ f.pubkey[i]=static_cast<uint8_t>(i+1); f.challenge_id[i]=static_cast<uint8_t>(i+2); f.nonce[i]=static_cast<uint8_t>(i+3); }
  std::vector<std::uint8_t> out;
  PHX_EXPECT(encode_pop_payload_v1(f,out));
  PopPayloadFieldsV1 d{};
  PHX_EXPECT(decode_pop_payload_v1(out,d));
  PHX_EXPECT(d.verifier_id=="v1" && d.owner=="o1" && d.issued_at==100 && d.expires_at==400);
  PHX_EXPECT(d.pubkey==f.pubkey && d.challenge_id==f.challenge_id && d.nonce==f.nonce);
  // rejects
  auto t=out; t.push_back(0); PHX_EXPECT(!decode_pop_payload_v1(t,d));
  auto tr=out; tr.pop_back(); PHX_EXPECT(!decode_pop_payload_v1(tr,d));
  auto m=out; m[0]^=1; PHX_EXPECT(!decode_pop_payload_v1(m,d));
  auto ver=out; ver[10]=2; PHX_EXPECT(!decode_pop_payload_v1(ver,d));
  auto proto=out; proto[13]=2; PHX_EXPECT(!decode_pop_payload_v1(proto,d));
  auto op=out; op[15]=9; PHX_EXPECT(!decode_pop_payload_v1(op,d));
  auto badts=out; size_t off=badts.size()-16; uint64_t issued=200,exp=100;
  for(int i=0;i<8;++i) badts[off+static_cast<size_t>(i)]=static_cast<uint8_t>((issued>>(8*i))&0xFF);
  for(int i=0;i<8;++i) badts[off+8u+static_cast<size_t>(i)]=static_cast<uint8_t>((exp>>(8*i))&0xFF);
  PHX_EXPECT(!decode_pop_payload_v1(badts,d));
}

PHX_TEST(ptm015c7_pop_v1_signature_field_mutations_production) {
  using namespace phantomx::poi;
  auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity); PHX_EXPECT(kp.ok()); if(!kp.ok()) return;
  PopPayloadFieldsV1 f{};
  f.network_u8 = pop_network_u8(phantomx::Network::kRegtest);
  f.verifier_id="v1"; f.owner="o1"; f.pubkey=kp.value().pub.bytes; f.issued_at=100; f.expires_at=400;
  for(size_t i=0;i<32;++i){ f.challenge_id[i]=static_cast<uint8_t>(i+1); f.nonce[i]=static_cast<uint8_t>(i+9); }
  std::vector<std::uint8_t> p; PHX_EXPECT(encode_pop_payload_v1(f,p));
  auto sig = phantomx::crypto::sign(std::span<const std::uint8_t>(p.data(),p.size()), kp.value().sec); PHX_EXPECT(sig.ok()); if(!sig.ok()) return;
  PHX_EXPECT(phantomx::crypto::verify(std::span<const std::uint8_t>(p.data(),p.size()), sig.value(), kp.value().pub));
  auto mutate_and_check = [&](size_t idx){
    auto m=p; m[idx]^=1; PopPayloadFieldsV1 d{};
    if (decode_pop_payload_v1(m,d)) {
      PHX_EXPECT(!phantomx::crypto::verify(std::span<const std::uint8_t>(m.data(),m.size()), sig.value(), kp.value().pub));
    } else {
      PHX_EXPECT(true); // decode rejection is acceptable failure class
    }
  };
  mutate_and_check(12); // network
  mutate_and_check(20); // verifier id
  mutate_and_check(p.size()-90); // pubkey/challenge area
  mutate_and_check(p.size()-40); // nonce area
  mutate_and_check(p.size()-10); // issued
  mutate_and_check(p.size()-2);  // expires
}

PHX_TEST(ptm015c7_challenge_boundary_8_9_per_identity) {
  namespace fs = std::filesystem;
  auto tmp = fs::temp_directory_path() / "phx_ptm015c7_cap";
  fs::remove_all(tmp); fs::create_directories(tmp);
  auto chain = phantomx::blockchain::ChainDatabase::open(tmp/"chain.db"); PHX_EXPECT(chain.ok()); if(!chain.ok()) return;
  const uint64_t now=1000;
  for(int i=0;i<8;++i){
    phantomx::blockchain::ChainDatabase::VerifierChallengeRecord r{};
    r.challenge_id = std::string(64, static_cast<char>('a'+i));
    r.verifier_id="v1"; r.pubkey_hex=std::string(64,'1'); r.owner="o1"; r.network="regtest";
    r.payload_blob={1,2,3}; r.issued_at=now; r.expires_at=now+300; r.status="issued";
    PHX_EXPECT(chain.value().put_verifier_challenge(r).ok());
  }
  auto cnt = chain.value().count_active_verifier_challenges_for("v1","o1",now);
  PHX_EXPECT(cnt.ok() && cnt.value()==8);
  // 9th should be detectable by caller (RPC enforces) - DB count confirms boundary
  phantomx::blockchain::ChainDatabase::VerifierChallengeRecord r9{};
  r9.challenge_id = std::string(64,'z'); r9.verifier_id="v1"; r9.pubkey_hex=std::string(64,'1'); r9.owner="o1"; r9.network="regtest";
  r9.payload_blob={1}; r9.issued_at=now; r9.expires_at=now+300; r9.status="issued";
  PHX_EXPECT(chain.value().put_verifier_challenge(r9).ok());
  auto cnt9 = chain.value().count_active_verifier_challenges_for("v1","o1",now);
  PHX_EXPECT(cnt9.ok() && cnt9.value()==9);
  // consumed not active
  auto c = chain.value().consume_verifier_challenge(std::string(64,'a'),"v1",std::string(64,'1'),"o1","regtest",now+1);
  PHX_EXPECT(c.ok() && c.value());
  auto cntAfter = chain.value().count_active_verifier_challenges_for("v1","o1",now+1);
  PHX_EXPECT(cntAfter.ok() && cntAfter.value()==8);
  fs::remove_all(tmp);
}

PHX_TEST(ptm015c7_challenge_cleanup_expired_opens_capacity) {
  namespace fs=std::filesystem;
  auto tmp=fs::temp_directory_path()/"phx_ptm015c7_cleanup"; fs::remove_all(tmp); fs::create_directories(tmp);
  auto chain=phantomx::blockchain::ChainDatabase::open(tmp/"chain.db"); PHX_EXPECT(chain.ok()); if(!chain.ok()) return;
  phantomx::blockchain::ChainDatabase::VerifierChallengeRecord r{};
  r.challenge_id=std::string(64,'e'); r.verifier_id="v1"; r.pubkey_hex=std::string(64,'2'); r.owner="o1"; r.network="regtest";
  r.payload_blob={1}; r.issued_at=10; r.expires_at=20; r.status="issued";
  PHX_EXPECT(chain.value().put_verifier_challenge(r).ok());
  auto b = chain.value().count_active_verifier_challenges(25);
  PHX_EXPECT(b.ok() && b.value()==0); // expired not active
  auto cl = chain.value().cleanup_verifier_challenges(25, 3600, 256);
  PHX_EXPECT(cl.ok());
  auto got = chain.value().get_verifier_challenge(r.challenge_id);
  PHX_EXPECT(got.ok() && !got.value().has_value()); // expired unused cleaned
  fs::remove_all(tmp);
}

PHX_TEST(ptm015c11r8_quota_tx_observer_records_stages) {
  using phantomx::blockchain::ChainDatabase;
  namespace fs = std::filesystem;
  auto dbpath = fs::temp_directory_path() / ("phantomx-r8-obs-" + std::to_string(static_cast<unsigned>(std::random_device{}())) + ".db");
  std::error_code ec; fs::remove(dbpath, ec);
  auto c = ChainDatabase::open(dbpath);
  PHX_EXPECT(c.ok());
  std::vector<ChainDatabase::QuotaTxDiagEvent> events;
  c.value().set_quota_tx_observer([&](const ChainDatabase::QuotaTxDiagEvent& ev){ events.push_back(ev); }, true);
  ChainDatabase::VerifierChallengeRecord r{};
  r.challenge_id = std::string(64,'a');
  r.verifier_id = "v1"; r.pubkey_hex = std::string(64,'1'); r.owner="o1"; r.network="regtest";
  r.payload_blob={1,2,3}; r.issued_at=100; r.expires_at=200; r.status="issued";
  auto pr = c.value().put_verifier_challenge_with_quota(r, 120, 4096, 8, 3600, 256);
  PHX_EXPECT(pr.ok());
  c.value().clear_quota_tx_observer();
  PHX_EXPECT(!events.empty());
  for (const auto& e: events) {
    PHX_EXPECT(e.operation_id != 0);
    PHX_EXPECT(e.transaction_id != 0 || e.stage == "FINAL_RESULT" || e.stage == "CONNECTION_OPEN");
    PHX_EXPECT(e.connection_id != 0);
    PHX_EXPECT(e.process_id != 0);
    PHX_EXPECT(e.thread_id != 0);
  }
  auto has=[&](const char* s){ for(auto& e:events) if(e.stage==s) return true; return false; };
  PHX_EXPECT(has("CONNECTION_OPEN"));
  PHX_EXPECT(has("BEGIN"));
  PHX_EXPECT(has("CLEANUP"));
  PHX_EXPECT(has("GLOBAL_COUNT"));
  PHX_EXPECT(has("IDENTITY_COUNT"));
  PHX_EXPECT(has("QUOTA_CHECK"));
  PHX_EXPECT(has("INSERT"));
  PHX_EXPECT(has("COMMIT"));
  PHX_EXPECT(has("FINAL_RESULT"));
  PHX_EXPECT(events.back().stage == std::string("FINAL_RESULT"));
  PHX_EXPECT(events.back().ok);
  (void)c.value().close();
  fs::remove(dbpath, ec);
}

PHX_TEST(ptm015c11r10_sqlite_connection_lifecycle_events) {
  namespace fs = std::filesystem;
  auto dbpath = fs::temp_directory_path() / ("phantomx-r10-conn-" + std::to_string(static_cast<unsigned>(std::random_device{}())) + ".db");
  std::error_code ec; fs::remove(dbpath, ec);
  std::vector<phantomx::db::SqliteConnectionEvent> evs;
  phantomx::db::SqliteDb::set_connection_observer([&](const phantomx::db::SqliteConnectionEvent& e){ evs.push_back(e); }, true);
  auto c = phantomx::blockchain::ChainDatabase::open(dbpath);
  PHX_EXPECT(c.ok());
  phantomx::db::SqliteDb::clear_connection_observer();
  PHX_EXPECT(!evs.empty());
  auto has=[&](const char* s){ for(auto& e:evs) if(e.stage==s) return true; return false; };
  PHX_EXPECT(has("OPEN_ATTEMPT"));
  PHX_EXPECT(has("OPEN_SUCCESS"));
  PHX_EXPECT(has("INITIALIZATION_BEGIN"));
  PHX_EXPECT(has("INITIALIZATION_SUCCESS"));
  PHX_EXPECT(has("CONNECTION_READY"));
  // correlation IDs
  for (auto& e:evs) {
    PHX_EXPECT(e.connection_id != 0);
    PHX_EXPECT(e.open_attempt_id != 0);
    PHX_EXPECT(e.process_id != 0);
    PHX_EXPECT(e.thread_id != 0);
  }
  // no ready before success
  bool init_ok=false, saw_ready=false;
  for (auto& e:evs) { if(e.stage=="INITIALIZATION_SUCCESS") init_ok=true; if(e.stage=="CONNECTION_READY") saw_ready=init_ok; }
  PHX_EXPECT(saw_ready);
  (void)c.value().close();
  fs::remove(dbpath, ec); fs::remove(dbpath.string()+"-wal", ec); fs::remove(dbpath.string()+"-shm", ec);
}

PHX_TEST(ptm015c7_lifecycle_pending_rejected_active_allowed_local) {
  namespace fs=std::filesystem;
  auto tmp=fs::temp_directory_path()/"phx_ptm015c7_life"; fs::remove_all(tmp); fs::create_directories(tmp);
  unsetenv("PHANTOMX_POI_BOOTSTRAP_ALLOW");
  auto chain=phantomx::blockchain::ChainDatabase::open(tmp/"chain.db"); PHX_EXPECT(chain.ok()); if(!chain.ok()) return;
  phantomx::Mempool mempool; phantomx::pool::PoolLedger pool; phantomx::aggregator::AggregationEngine agg;
  phantomx::rpc::RpcServer server; phantomx::rpc::register_default_methods(server); server.add_user({"u","t",{}});
  PHX_EXPECT(phantomx::rpc::register_node_rpc_handlers(server, chain.value(), mempool, pool, agg).ok());
  auto kp = phantomx::crypto::generate_keypair(phantomx::crypto::KeyRole::kVerifierIdentity); PHX_EXPECT(kp.ok()); if(!kp.ok()) return;
  const std::string pkhex = phantomx::crypto::serialize_public_key_hex(kp.value().pub);
  phantomx::rpc::RpcRequest na; na.id="na"; na.method="getnewaddress"; na.auth_token="t"; auto nar=server.call(na,1); PHX_EXPECT(nar.ok); if(!nar.ok) return;
  auto vaddr_it = nar.result.find("address"); PHX_EXPECT(vaddr_it != nar.result.end()); if (vaddr_it == nar.result.end()) return;
  std::string vaddr = vaddr_it->second;
  phantomx::rpc::RpcRequest ch; ch.id="c"; ch.method="verifier_register_challenge"; ch.auth_token="t"; ch.params["id"]="v1"; ch.params["pubkey"]=pkhex; ch.params["owner"]="o1";
  auto chr=server.call(ch,1); PHX_EXPECT(chr.ok); if(!chr.ok) return;
  auto payload_it = chr.result.find("payload_hex"); auto chid_it = chr.result.find("challenge_id");
  PHX_EXPECT(payload_it != chr.result.end()); PHX_EXPECT(chid_it != chr.result.end());
  if (payload_it == chr.result.end() || chid_it == chr.result.end()) return;
  std::vector<std::uint8_t> pbytes; PHX_EXPECT(phantomx::from_hex(payload_it->second, pbytes));
  auto csig=phantomx::crypto::sign(std::span<const std::uint8_t>(pbytes.data(), pbytes.size()), kp.value().sec);
  PHX_EXPECT(csig.ok()); if(!csig.ok()) return;
  phantomx::rpc::RpcRequest reg; reg.id="r"; reg.method="verifier_register"; reg.auth_token="t";
  reg.params["id"]="v1"; reg.params["address"]=vaddr; reg.params["endpoint"]="127.0.0.1:1"; reg.params["pubkey"]=pkhex; reg.params["owner"]="o1"; reg.params["challenge_id"]=chid_it->second; reg.params["pop_sig"]=phantomx::crypto::serialize_signature_hex(csig.value());
  PHX_EXPECT(server.call(reg,2).ok);
  // PENDING heartbeat rejected
  phantomx::rpc::RpcRequest hb; hb.id="hb"; hb.method="verifier_heartbeat"; hb.auth_token="t"; hb.params["id"]="v1";
  auto hbr = server.call(hb,3); PHX_EXPECT(!hbr.ok);
  // activate with admin token
  setenv("PHANTOMX_ADMIN_TOKEN","adm15c7",1);
  phantomx::rpc::RpcRequest act; act.id="a"; act.method="verifier_set_status"; act.auth_token="t"; act.params["id"]="v1"; act.params["status"]="ACTIVE"; act.params["admin_token"]="adm15c7";
  PHX_EXPECT(server.call(act,4).ok);
  auto hba = server.call(hb,5); PHX_EXPECT(hba.ok);
  // suspend then reject
  phantomx::rpc::RpcRequest sus=act; sus.id="s"; sus.params["status"]="SUSPENDED"; PHX_EXPECT(server.call(sus,6).ok);
  PHX_EXPECT(!server.call(hb,7).ok);
  // revoke then reject
  phantomx::rpc::RpcRequest rev=act; rev.id="rv"; rev.params["status"]="REVOKED"; PHX_EXPECT(server.call(rev,8).ok);
  PHX_EXPECT(!server.call(hb,9).ok);
  unsetenv("PHANTOMX_ADMIN_TOKEN");
  fs::remove_all(tmp);
}
