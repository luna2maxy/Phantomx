#include "phantomx/rpc/node_handlers.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <cmath>
#include <mutex>
#include <thread>

#include <jansson.h>

#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/blockchain/chain.hpp"
#include "phantomx/primitives/block.hpp"
#include "phantomx/primitives/merkle.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/common/types.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/consensus/pow.hpp"
#include "phantomx/poi/score.hpp"
#include "phantomx/pool/pool.hpp"
#include "phantomx/primitives/bech32m.hpp"
#include "phantomx/primitives/merkle.hpp"
#include "phantomx/rewards/rewards.hpp"
#include "phantomx/utxo/validation.hpp"
#include "phantomx/wallet/wallet.hpp"
#include "phantomx/common/paths.hpp"
#include "phantomx/poi/verifier_pop.hpp"

namespace phantomx::rpc {
extern thread_local std::string g_rpc_remote_ip;
namespace {

std::atomic<bool> g_poi_running{false};
std::atomic<std::uint64_t> g_poi_bandwidth_bytes{100ULL * 1024ULL * 1024ULL};
std::string g_poi_bandwidth_unit = "MB";
std::string g_poi_reward_address;
std::atomic<bool> g_poi_autoproduce{true}; // produce blocks while PoI running
std::atomic<bool> g_poi_producer_started{false};
std::atomic<std::uint64_t> g_poi_blocks_found{0};
std::atomic<std::uint64_t> g_poi_last_block_at{0};
std::string g_poi_last_block_hash;
std::atomic<std::uint64_t> g_poi_finalized_height{0};
std::string g_poi_finalized_hash;
std::atomic<std::uint64_t> g_poi_finality_quorum{1}; // bootstrap Q=1
std::atomic<std::uint64_t> g_poi_q_min{1}; // configurable minimum quorum
std::atomic<std::uint64_t> g_poi_verifiers_online{0};
std::uint64_t g_poi_last_relay_count = 0;
std::uint64_t g_poi_last_relay_ts = 0;
double g_poi_last_relay_ratio = -1.0;
constexpr std::uint64_t kPoiHeartbeatMinIntervalSec = 5;
constexpr std::uint64_t kPoiMaxAttestationsPerBlock = 256;
constexpr std::uint64_t kPoiAttestationMaxAgeSec = 3600;

struct PoiBlockAttestation {
  std::string verifier_pubkey_hex;
  std::string signature_hex;
  std::uint64_t timestamp = 0;
};

struct PoiBlockFinality {
  std::uint64_t height = 0;
  std::string block_hash;
  std::uint64_t quorum_required = 1;
  bool finalized = false;
  std::uint64_t finalized_at = 0;
  std::vector<PoiBlockAttestation> attestations;
};

std::unordered_map<std::string, PoiBlockFinality> g_poi_block_finality; // key=block_hash


struct PoiParticipant {
  std::string id;
  std::string owner_address;  // reward/identity address; same owner can have multiple machines
  std::string machine_id;     // unique per machine
  std::string source_ip;      // last observed RPC source IP (anti-abuse accounting)
  std::uint64_t bandwidth_bytes = 100ULL * 1024ULL * 1024ULL;
  std::string bandwidth_unit = "MB";
  std::uint64_t last_heartbeat = 0;
  bool running = false;
  double latency_ms = -1.0;   // <0 = not available
  double relay_ratio = -1.0;  // <0 = not available
  std::uint64_t uptime_ok = 0;
  std::uint64_t uptime_expected = 0;
  std::uint64_t uptime_window_start = 0;
};

std::recursive_mutex g_poi_mu;
std::mutex g_poi_persist_mu;
std::unordered_map<std::string, PoiParticipant> g_poi_participants;

struct PoiIpBinding {
  std::string ip;
  std::string owner_address;
  std::string participant_id;
  std::uint64_t last_seen = 0;
};
std::unordered_map<std::string, PoiIpBinding> g_poi_ip_bindings; // key=ip

static std::string poi_normalize_address(std::string a) {
  while (!a.empty() && (a.back()==' '||a.back()=='\n'||a.back()=='\r'||a.back()=='\t')) a.pop_back();
  size_t i=0; while(i<a.size() && (a[i]==' '||a[i]=='\n'||a[i]=='\r'||a[i]=='\t')) ++i;
  return a.substr(i);
}

static std::string poi_effective_owner(const std::string& participant, const std::unordered_map<std::string,std::string>& params) {
  auto it = params.find("owner_address");
  if (it != params.end() && !poi_normalize_address(it->second).empty()) return poi_normalize_address(it->second);
  it = params.find("reward_address");
  if (it != params.end() && !poi_normalize_address(it->second).empty()) return poi_normalize_address(it->second);
  // Default: participant id acts as identity for local/dev; explicit owner_address should be supplied by wallets.
  return poi_normalize_address(participant);
}

static std::string poi_effective_machine(const std::string& participant, const std::unordered_map<std::string,std::string>& params) {
  auto it = params.find("machine_id");
  if (it != params.end() && !it->second.empty()) return it->second;
  it = params.find("node_id");
  if (it != params.end() && !it->second.empty()) return it->second;
  return participant; // backward compatible
}

static bool poi_ip_reject_conflict(const std::string& ip, const std::string& owner_address, [[maybe_unused]] const std::string& participant_id, std::string& reason) {
  if (ip.empty()) return false; // local/internal calls; cannot enforce IP policy
  auto it = g_poi_ip_bindings.find(ip);
  if (it == g_poi_ip_bindings.end()) return false;
  const auto now = static_cast<std::uint64_t>(std::time(nullptr));
  // Expire stale bindings outside heartbeat window to avoid permanent lockout from old tests.
  if (it->second.last_seen != 0 && now > it->second.last_seen + 600ULL) {
    g_poi_ip_bindings.erase(it);
    return false;
  }
  // Requirement: 1 IP using 2 addresses => reject second address.
  // Same owner address from same IP with different machine/participant is allowed (multi-machine same wallet).
  if (it->second.owner_address != owner_address) {
    reason = "ip_already_bound_to_other_address";
    return true;
  }
  return false;
}

static void poi_ip_bind(const std::string& ip, const std::string& owner_address, const std::string& participant_id, std::uint64_t now_sec) {
  if (ip.empty()) return;
  g_poi_ip_bindings[ip] = PoiIpBinding{ip, owner_address, participant_id, now_sec};
}

struct PoiIdentityBinding {
  std::string owner_address;
  std::string identity_pubkey_hex;
  std::uint64_t first_seen = 0;
  std::uint64_t last_seen = 0;
};
std::unordered_map<std::string, PoiIdentityBinding> g_poi_identity_by_owner;   // owner_address -> binding
std::unordered_map<std::string, std::string> g_poi_owner_by_identity_pubkey;  // pubkey_hex -> owner_address
std::string g_poi_identity_path;

static void poi_save_identity_bindings() {
  if (g_poi_identity_path.empty()) return;
  json_t* root = json_object();
  json_t* arr = json_array();
  for (const auto& [owner, b] : g_poi_identity_by_owner) {
    json_t* o = json_object();
    json_object_set_new(o, "owner_address", json_string(b.owner_address.c_str()));
    json_object_set_new(o, "identity_pubkey_hex", json_string(b.identity_pubkey_hex.c_str()));
    json_object_set_new(o, "first_seen", json_integer(static_cast<json_int_t>(b.first_seen)));
    json_object_set_new(o, "last_seen", json_integer(static_cast<json_int_t>(b.last_seen)));
    json_array_append_new(arr, o);
  }
  json_object_set_new(root, "bindings", arr);
  char* s = json_dumps(root, JSON_INDENT(2));
  if (s) { std::ofstream f(g_poi_identity_path, std::ios::binary|std::ios::trunc); f << s; free(s); }
  json_decref(root);
}

[[maybe_unused]] static void poi_load_identity_bindings() {
  g_poi_identity_by_owner.clear();
  g_poi_owner_by_identity_pubkey.clear();
  if (g_poi_identity_path.empty()) return;
  std::ifstream f(g_poi_identity_path, std::ios::binary);
  if (!f.good()) return;
  std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  json_error_t je{}; json_t* root = json_loads(c.c_str(), 0, &je);
  if (!root) return;
  json_t* arr = json_object_get(root, "bindings");
  if (json_is_array(arr)) {
    size_t i; json_t* v;
    json_array_foreach(arr, i, v) {
      const char* owner = json_string_value(json_object_get(v, "owner_address"));
      const char* pk = json_string_value(json_object_get(v, "identity_pubkey_hex"));
      if (!owner || !pk || !*owner || !*pk) continue;
      PoiIdentityBinding b;
      b.owner_address = owner;
      b.identity_pubkey_hex = pk;
      b.first_seen = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "first_seen")));
      b.last_seen = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "last_seen")));
      g_poi_identity_by_owner[b.owner_address] = b;
      g_poi_owner_by_identity_pubkey[b.identity_pubkey_hex] = b.owner_address;
    }
  }
  json_decref(root);
}

static std::string poi_identity_message(const std::string& owner, const std::string& machine, const std::string& participant, std::uint64_t ts) {
  return "PHX/poi/identity/v1|" + owner + "|" + machine + "|" + participant + "|" + std::to_string(ts);
}

static Result<void> poi_verify_identity_claim(const std::unordered_map<std::string, std::string>& params,
                                              const std::string& owner, const std::string& machine,
                                              const std::string& participant, std::uint64_t now_sec) {
  auto pk_it = params.find("identity_pubkey");
  auto sig_it = params.find("identity_sig");
  auto ts_it = params.find("identity_ts");
  if (pk_it == params.end() || sig_it == params.end() || ts_it == params.end()) {
    // Backward-compatible local/dev mode: unsigned claims allowed only when no remote IP (local call).
    // Remote RPC claims must be signed unless explicitly allowed for local debugging.
    if (!g_rpc_remote_ip.empty()) {
      const char* allow = std::getenv("PHANTOMX_POI_ALLOW_UNSIGNED_IDENTITY");
      if (!(allow && std::string(allow) == "1")) {
        return MakeError(ErrorCode::kUnauthorized, "missing identity_pubkey/identity_sig/identity_ts");
      }
    }
    return Result<void>{};
  }
  if (owner.empty()) return MakeError(ErrorCode::kInvalidArgument, "missing owner_address");
  std::uint64_t ts = 0;
  try { ts = std::stoull(ts_it->second); } catch (...) { return MakeError(ErrorCode::kInvalidArgument, "invalid identity_ts"); }
  const std::uint64_t max_skew = 300;
  if (ts > now_sec + max_skew || now_sec > ts + max_skew) return MakeError(ErrorCode::kInvalidArgument, "identity_ts outside allowed skew");
  auto pk = crypto::parse_public_key_hex(pk_it->second);
  if (!pk.ok()) return pk.error();
  auto sig_bytes = crypto::parse_signature_hex(sig_it->second);
  if (!sig_bytes.ok()) return sig_bytes.error();
  const auto msg = poi_identity_message(owner, machine, participant, ts);
  const std::span<const std::uint8_t> msg_span(reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size());
  if (!crypto::verify(msg_span, sig_bytes.value(), pk.value())) {
    return MakeError(ErrorCode::kUnauthorized, "invalid identity signature");
  }
  const std::string pk_hex = pk_it->second;
  auto by_owner = g_poi_identity_by_owner.find(owner);
  if (by_owner != g_poi_identity_by_owner.end() && by_owner->second.identity_pubkey_hex != pk_hex) {
    return MakeError(ErrorCode::kForbidden, "owner_address_bound_to_different_identity_pubkey");
  }
  auto by_pk = g_poi_owner_by_identity_pubkey.find(pk_hex);
  if (by_pk != g_poi_owner_by_identity_pubkey.end() && by_pk->second != owner) {
    return MakeError(ErrorCode::kForbidden, "identity_pubkey_bound_to_different_owner");
  }
  PoiIdentityBinding b;
  b.owner_address = owner;
  b.identity_pubkey_hex = pk_hex;
  b.first_seen = (by_owner == g_poi_identity_by_owner.end() || by_owner->second.first_seen == 0) ? now_sec : by_owner->second.first_seen;
  b.last_seen = now_sec;
  g_poi_identity_by_owner[owner] = b;
  g_poi_owner_by_identity_pubkey[pk_hex] = owner;
  poi_save_identity_bindings();
  return Result<void>{};
}


[[maybe_unused]] constexpr double kPoiMRef = phantomx::poi::kPoiMRef;
constexpr double kPoiKEpoch = 10.0;
constexpr std::uint64_t kPoiHeartbeatWindowSec = 600; // liveness window
constexpr std::uint64_t kPoiHeartbeatIntervalSec = 60;
constexpr std::uint64_t kPoiUptimeWindowSec = phantomx::poi::kPoiUptimeWindowSec; // 24h reward window

double poi_bandwidth_mbps(std::uint64_t bandwidth_bytes, const std::string& unit) {
  // Config/UI stores bandwidth as bytes with unit label MB/GB/TB (MB/s, GB/s, TB/s intent).
  // Convert to Mbps for scoring: Mbps = (bytes / 1024^2) * 8 for MB, etc.
  if (bandwidth_bytes == 0) return 0.0;
  std::string u = unit;
  for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  const double bytes = static_cast<double>(bandwidth_bytes);
  if (!std::isfinite(bytes) || bytes < 0.0) return -1.0;
  double mbps = -1.0;
  if (u == "MBPS" || u == "MBIT" || u == "MBIT/S") mbps = bytes;
  else if (u == "GBPS" || u == "GBIT/S") mbps = bytes * 1000.0;
  else if (u == "KBPS") mbps = bytes / 1000.0;
  else if (u == "TB") mbps = (bytes / (1024.0 * 1024.0 * 1024.0 * 1024.0)) * 1024.0 * 1024.0 * 8.0;
  else if (u == "GB") mbps = (bytes / (1024.0 * 1024.0 * 1024.0)) * 1024.0 * 8.0;
  else mbps = (bytes / (1024.0 * 1024.0)) * 8.0; // MB/s default
  return (std::isfinite(mbps) && mbps >= 0.0) ? mbps : -1.0;
}

// Legacy helper removed: use poi_bandwidth_mbps(bytes, unit).

double poi_uptime_ratio(const PoiParticipant& p, std::uint64_t now_sec) {
  if (p.uptime_window_start == 0 || now_sec < p.uptime_window_start) return -1.0;
  const std::uint64_t elapsed = now_sec - p.uptime_window_start;
  const std::uint64_t expected = std::max<std::uint64_t>(1, std::min<std::uint64_t>(kPoiUptimeWindowSec, elapsed) / kPoiHeartbeatIntervalSec);
  const std::uint64_t ok = std::min<std::uint64_t>(p.uptime_ok, expected);
  return static_cast<double>(ok) / static_cast<double>(expected);
}

struct PoiScoreResult {
  bool available = false;
  double score = 0.0;
  bool partial = false;
  std::string bandwidth_component = "Not Available";
  std::string latency_component = "Not Available";
  std::string uptime_component = "Not Available";
  std::string relay_component = "Not Available";
  std::string network_component = "Not Available";
  phantomx::poi::PoiCompositeScore raw{};
};

PoiScoreResult poi_compute_score(std::uint64_t bandwidth_bytes, const std::string& bandwidth_unit,
                                 double latency_ms, double relay_ratio, double uptime_ratio,
                                 std::uint64_t active_miners) {
  phantomx::poi::PoiCompositeInput in;
  const double mbps = poi_bandwidth_mbps(bandwidth_bytes, bandwidth_unit);
  in.bandwidth_mbps = (std::isfinite(mbps) && mbps > 0.0) ? static_cast<std::uint64_t>(std::llround(mbps)) : 0;
  in.bandwidth_valid = (std::isfinite(mbps) && mbps > 0.0);
  in.latency_ms = latency_ms;
  in.uptime_ratio = uptime_ratio;
  in.relay_ratio = relay_ratio;
  in.active_miners = active_miners;
  in.network_valid = true;
  in.bw_ref_mbps = phantomx::poi::kPoiBwRefMbps;
  in.lat_ref_ms = phantomx::poi::kPoiLatRefMs;
  in.m_ref = phantomx::poi::kPoiMRef;
  const auto s = phantomx::poi::poi_composite_score_v2(in);
  PoiScoreResult r;
  r.raw = s;
  r.available = s.available;
  r.partial = s.partial;
  r.score = s.score;
  auto pct = [](double v) { return v >= 0.0 ? std::to_string(static_cast<int>(std::lround(v * 100.0))) : std::string("Not Available"); };
  r.bandwidth_component = pct(s.bandwidth);
  r.latency_component = pct(s.latency);
  r.uptime_component = pct(s.uptime);
  r.relay_component = pct(s.relay);
  r.network_component = pct(s.network);
  return r;
}

std::uint64_t poi_active_miners(std::uint64_t now_sec) {
  std::scoped_lock lk(g_poi_mu);
  std::uint64_t n = 0;
  for (const auto& [_, p] : g_poi_participants) {
    if (!p.running) continue;
    if (p.last_heartbeat == 0) continue;
    if (now_sec >= p.last_heartbeat && (now_sec - p.last_heartbeat) <= kPoiHeartbeatWindowSec) ++n;
  }
  return n;
}

std::uint64_t poi_epoch(std::uint64_t active_miners) {
  return phantomx::poi::poi_epoch_level(active_miners, static_cast<std::uint64_t>(kPoiKEpoch));
}

static double poi_owner_aggregate_score(const std::string& owner_address, std::uint64_t now_sec, double fallback_score) {
  if (owner_address.empty()) return fallback_score;
  double best = -1.0;
  double sum = 0.0;
  std::uint64_t n = 0;
  for (const auto& [_, p] : g_poi_participants) {
    if (p.owner_address != owner_address) continue;
    if (!p.running || p.last_heartbeat == 0) continue;
    if (now_sec < p.last_heartbeat || (now_sec - p.last_heartbeat) > kPoiHeartbeatWindowSec) continue;
    const double up = poi_uptime_ratio(p, now_sec);
    phantomx::poi::PoiCompositeInput in;
    in.bandwidth_mbps = static_cast<std::uint64_t>(std::llround(std::max(0.0, poi_bandwidth_mbps(p.bandwidth_bytes, p.bandwidth_unit))));
    in.bandwidth_valid = in.bandwidth_mbps > 0;
    in.latency_ms = p.latency_ms;
    in.uptime_ratio = up;
    in.relay_ratio = p.relay_ratio;
    in.active_miners = poi_active_miners(now_sec);
    in.network_valid = true;
    auto sr = phantomx::poi::poi_composite_score_v2(in);
    if (!sr.available) continue;
    sum += sr.score;
    ++n;
    if (sr.score > best) best = sr.score;
  }
  if (n == 0) return fallback_score;
  // Multi-machine same wallet: aggregate contribution, capped to 100. Use sum with diminishing not needed; cap preserves bounds.
  return std::min(100.0, sum);
}

std::uint64_t poi_target_block_interval_ms(std::uint64_t base_ms, double avg_score, std::uint64_t epoch) {
  return phantomx::poi::poi_target_block_interval_ms(base_ms, avg_score, epoch);
}

double poi_relay_ratio(network::P2PNode* p2p) {
  if (!p2p) return -1.0;
  const auto now = static_cast<std::uint64_t>(std::time(nullptr));
  const auto count = p2p->relay_message_count();
  const auto ready = p2p->ready_peer_count();
  std::scoped_lock lk(g_poi_mu);
  if (g_poi_last_relay_ts == 0) {
    g_poi_last_relay_ts = now;
    g_poi_last_relay_count = count;
    g_poi_last_relay_ratio = -1.0; // wait for first delta window
    return g_poi_last_relay_ratio;
  }
  const auto dt = now > g_poi_last_relay_ts ? (now - g_poi_last_relay_ts) : 1ULL;
  const auto delta = count >= g_poi_last_relay_count ? (count - g_poi_last_relay_count) : 0ULL;
  g_poi_last_relay_ts = now;
  g_poi_last_relay_count = count;
  if (ready == 0 && delta == 0) { g_poi_last_relay_ratio = -1.0; return g_poi_last_relay_ratio; }
  const double expected = std::max(1.0, static_cast<double>(ready) * std::max(1.0, static_cast<double>(dt)));
  g_poi_last_relay_ratio = std::clamp(static_cast<double>(delta) / expected, 0.0, 1.0);
  return g_poi_last_relay_ratio;
}

double poi_latency_ms(network::P2PNode* p2p) {
  if (!p2p) return -1.0;
  const double avg = p2p->average_latency_ms();
  return avg >= 0.0 ? avg : -1.0;
}

// Caller must hold g_poi_mu.
void poi_upsert_participant_locked(const std::string& id, std::uint64_t bandwidth_bytes, const std::string& unit, bool running) {
  auto& p = g_poi_participants[id.empty() ? "local" : id];
  if (p.id.empty()) p.id = id.empty() ? "local" : id;
  if (bandwidth_bytes > 0) p.bandwidth_bytes = bandwidth_bytes;
  if (!unit.empty()) p.bandwidth_unit = unit;
  p.running = running;
  const auto now_sec = static_cast<std::uint64_t>(std::time(nullptr));
  p.last_heartbeat = now_sec;
  if (p.uptime_window_start == 0) p.uptime_window_start = now_sec;
  // Count liveness heartbeat for uptime window (deterministic window accounting).
  // NOTE: this is self-reported liveness, not cryptographically verified measurement proof.
  if (p.uptime_expected < (kPoiUptimeWindowSec / kPoiHeartbeatIntervalSec)) ++p.uptime_expected;
  if (p.uptime_ok < p.uptime_expected) ++p.uptime_ok;
}

[[maybe_unused]] void poi_upsert_participant(const std::string& id, std::uint64_t bandwidth_bytes, const std::string& unit, bool running) {
  std::scoped_lock lk(g_poi_mu);
  poi_upsert_participant_locked(id, bandwidth_bytes, unit, running);
}

RpcResponse ok(std::string id, std::unordered_map<std::string, std::string> result) {
  RpcResponse r;
  r.id = std::move(id);
  r.ok = true;
  r.code = RpcErrorCode::kOk;
  r.result = std::move(result);
  return r;
}

RpcResponse err(std::string id, RpcErrorCode code, std::string msg) {
  RpcResponse r;
  r.id = std::move(id);
  r.ok = false;
  r.code = code;
  r.error_message = std::move(msg);
  return r;
}

// Local convenience mirror of phantomx::to_hex for Hash256Bytes spans.
std::string to_hex_local(std::span<const std::uint8_t> bytes) { return phantomx::to_hex(bytes); }

// Parse the "public_key_hex" request param into a PublicKey.
Result<crypto::PublicKey> parse_pub(const RpcRequest& req) {
  const auto it = req.params.find("public_key_hex");
  if (it == req.params.end()) return MakeError(ErrorCode::kInvalidArgument, "missing param public_key_hex");
  auto parsed = crypto::parse_public_key_hex(it->second);
  if (!parsed.ok()) return parsed.error();
  return parsed.value();
}

std::vector<std::uint8_t> pub32(const crypto::PublicKey& pub) {
  return std::vector<std::uint8_t>(pub.bytes.begin(), pub.bytes.end());
}

// Serialize UTXO set entries matching `pub` to a jansson JSON array.
json_t* utxos_to_json(const blockchain::ChainDatabase& chain, const crypto::PublicKey& pub) {
  auto utxos = chain.load_utxo_set();
  if (!utxos.ok()) return json_array();
  auto tip = chain.tip();
  const std::uint32_t tip_h = (tip.ok() && tip.value().has_value()) ? tip.value()->height : 0u;
  const auto script = script_pubkey_for_pubkey(pub32(pub));
  json_t* arr = json_array();
  for (const auto& [outpoint, entry] : utxos.value().entries()) {
    if (entry.script_pubkey != script) continue;
    const bool immature = entry.coinbase && (static_cast<std::uint64_t>(tip_h) < static_cast<std::uint64_t>(entry.height) + kCoinbaseMaturity);
    const std::uint64_t conf = tip_h >= entry.height ? (static_cast<std::uint64_t>(tip_h - entry.height) + 1) : 0;
    json_t* obj = json_object();
    json_object_set_new(obj, "txid", json_string(to_hex_local(outpoint.txid).c_str()));
    json_object_set_new(obj, "vout", json_integer(outpoint.vout));
    json_object_set_new(obj, "amount", json_string(std::to_string(entry.amount_atoms).c_str()));
    json_object_set_new(obj, "script_pubkey_hex", json_string(to_hex_local(entry.script_pubkey).c_str()));
    json_object_set_new(obj, "height", json_integer(entry.height));
    json_object_set_new(obj, "coinbase", json_boolean(entry.coinbase));
    json_object_set_new(obj, "immature", json_boolean(immature));
    json_object_set_new(obj, "confirmations", json_integer(static_cast<json_int_t>(conf)));
    json_array_append(arr, obj);
    json_decref(obj);
  }
  return arr;
}

std::string dump_utxos_json(const blockchain::ChainDatabase& chain, const crypto::PublicKey& pub) {
  json_t* arr = utxos_to_json(chain, pub);
  char* text = json_dumps(arr, JSON_COMPACT);
  std::string out = text != nullptr ? text : "[]";
  if (text != nullptr) free((void*)text);
  json_decref(arr);
  return out;
}

struct BalanceSplit {
  std::uint64_t spendable = 0;
  std::uint64_t immature = 0;
  std::uint64_t total() const { return spendable + immature; }
};

BalanceSplit balance_for_pub(const blockchain::ChainDatabase& chain, const crypto::PublicKey& pub) {
  BalanceSplit out;
  auto utxos = chain.load_utxo_set();
  if (!utxos.ok()) return out;
  auto tip = chain.tip();
  const std::uint32_t tip_h = (tip.ok() && tip.value().has_value()) ? tip.value()->height : 0u;
  const auto script = script_pubkey_for_pubkey(pub32(pub));
  for (const auto& [_, entry] : utxos.value().entries()) {
    if (entry.script_pubkey != script) continue;
    const bool is_immature = entry.coinbase && (static_cast<std::uint64_t>(tip_h) < static_cast<std::uint64_t>(entry.height) + kCoinbaseMaturity);
    if (is_immature) out.immature += entry.amount_atoms;
    else out.spendable += entry.amount_atoms;
  }
  return out;
}

struct FoundTx { Transaction tx; std::uint32_t height = 0; };
// Scans the active chain for a transaction id.
Result<FoundTx> find_tx(const blockchain::ChainDatabase& chain, const Hash256Bytes& txid) {
  auto tip = chain.tip();
  if (!tip.ok()) return tip.error();
  if (!tip.value().has_value()) return MakeError(ErrorCode::kNotFound, "no chain tip");
  for (std::uint32_t h = 0; h <= tip.value()->height; ++h) {
    auto hash = chain.get_hash_by_height(h);
    if (!hash.ok()) return hash.error();
    if (!hash.value().has_value()) continue;
    auto block = chain.read_block(hash.value().value());
    if (!block.ok()) continue;
    for (const auto& tx : block.value().transactions) {
      if (phantomx::txid(tx) == txid) return FoundTx{tx, h};
    }
  }
  return MakeError(ErrorCode::kNotFound, "tx not found");
}

}  // namespace

Result<void> register_node_rpc_handlers(RpcServer& server, blockchain::ChainDatabase& chain, phantomx::Mempool& mempool, pool::PoolLedger& pool, aggregator::AggregationEngine& aggregator, std::string aggregator_state_path, std::string aggregator_db_path, network::P2PNode* p2p, consensus::BftEngine* bft, const crypto::KeyPair* local_bft_key, std::function<void(const consensus::BlockProposal&)> on_local_proposal, std::function<std::array<std::uint8_t, 32>(std::uint64_t, std::uint64_t)> proposer_selector, phantomx::Network network) {
  // Node status
  server.register_method("getnodeinfo", [&chain, &mempool](const RpcRequest& req) {
    auto tip = chain.tip();
    std::ostringstream height_str;
    height_str << (tip.ok() && tip.value().has_value() ? tip.value()->height : 0);
    std::unordered_map<std::string, std::string> result{
        {"app", "phantomxd"}, {"version", "0.1.0"}, {"peers", "0"}, {"syncing", "false"},
        {"chain_height", height_str.str()},
        {"mempool_size", std::to_string(mempool.size())},
    };
    if (tip.ok() && tip.value().has_value()) result["tip_hash"] = to_hex_local(tip.value()->hash);
    return ok(req.id, result);
  });
  server.register_method("getnetworkinfo", [&chain, p2p, network](const RpcRequest& req) {
    auto tip = chain.tip();
    std::unordered_map<std::string,std::string> r{{"version", "0.1.0"},
                       {"best_height", std::to_string(tip.ok() && tip.value().has_value() ? tip.value()->height : 0)},
                       {"subversion", "/PhantomX:0.1.0(" + std::string(network_name(network)) + ")"},
                       {"network", std::string(network_name(network))}};
    r["p2p_enabled"] = p2p ? "true" : "false";
    r["connections"] = p2p ? std::to_string(p2p->peer_count()) : "0";
    r["peer_count"] = p2p ? std::to_string(p2p->peer_count()) : "0";
    r["ready_peer_count"] = p2p ? std::to_string(p2p->ready_peer_count()) : "0";
    return ok(req.id, r);
  });
  server.register_method("getblockchaininfo", [&chain, p2p, network](const RpcRequest& req) {
    auto tip = chain.tip();
    const std::uint32_t blocks = tip.ok() && tip.value().has_value() ? tip.value()->height : 0;
    const std::uint32_t headers = blocks; // no header sync target exposed yet
    const double progress = headers == 0 ? 1.0 : static_cast<double>(blocks) / static_cast<double>(headers);
    return ok(req.id, {{"chain", std::string(network_name(network))},
                       {"network", std::string(network_name(network))},
                       {"blocks", std::to_string(blocks)},
                       {"headers", std::to_string(headers)},
                       {"verificationprogress", std::to_string(progress > 1.0 ? 1.0 : progress)},
                       {"initialblockdownload", blocks < headers ? "true" : "false"},
                       {"bestblockhash", tip.ok() && tip.value().has_value() ? to_hex_local(tip.value()->hash) : ""},
                       {"connections", p2p ? std::to_string(p2p->peer_count()) : "0"}});
  });
  server.register_method("getbestblockhash", [&chain](const RpcRequest& req) {
    auto tip = chain.tip();
    return ok(req.id, {{"best_block_hash",
                        tip.ok() && tip.value().has_value() ? to_hex_local(tip.value()->hash) : ""}});
  });
  server.register_method("getblockcount", [&chain](const RpcRequest& req) {
    auto tip = chain.tip();
    return ok(req.id, {{"block_count",
                        std::to_string(tip.ok() && tip.value().has_value() ? tip.value()->height : std::numeric_limits<std::uint32_t>::max())}});
  });
  server.register_method("getblockhash", [&chain](const RpcRequest& req) {
    const auto it = req.params.find("height");
    if (it == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing param height");
    std::uint32_t height = 0;
    try {
      const auto v = std::stoull(it->second);
      if (v > std::numeric_limits<std::uint32_t>::max()) return err(req.id, RpcErrorCode::kInvalidParams, "invalid height");
      height = static_cast<std::uint32_t>(v);
    } catch (...) { return err(req.id, RpcErrorCode::kInvalidParams, "invalid height"); }
    auto hash = chain.get_hash_by_height(height);
    if (!hash.ok()) return err(req.id, RpcErrorCode::kInternalError, hash.error().to_string());
    if (!hash.value().has_value()) return err(req.id, RpcErrorCode::kNotFound, "block not found");
    return ok(req.id, {{"blockhash", to_hex_local(hash.value().value())}});
  });
  server.register_method("getblock", [&chain](const RpcRequest& req) {
    const auto it = req.params.find("hash");
    if (it == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing param hash");
    std::vector<std::uint8_t> hash_bytes;
    if (!from_hex(it->second, hash_bytes) || hash_bytes.size() != 32) {
      return err(req.id, RpcErrorCode::kInvalidParams, "invalid hash param");
    }
    Hash256Bytes id{};
    std::copy(hash_bytes.begin(), hash_bytes.end(), id.begin());
    auto block = chain.read_block(id);
    if (!block.ok()) return err(req.id, RpcErrorCode::kInternalError, block.error().to_string());
    return ok(req.id, {{"block_hex", to_hex_local(serialize_block(block.value()))},
                       {"tx_count", std::to_string(block.value().transactions.size())},
                       {"height", std::to_string(block.value().header.height)}});
  });
  server.register_method("gettransaction", [&chain, &mempool](const RpcRequest& req) {
    const auto it = req.params.find("txid");
    if (it == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing param txid");
    std::vector<std::uint8_t> txid_bytes;
    if (!from_hex(it->second, txid_bytes) || txid_bytes.size() != 32) {
      return err(req.id, RpcErrorCode::kInvalidParams, "invalid txid param");
    }
    Hash256Bytes id{};
    std::copy(txid_bytes.begin(), txid_bytes.end(), id.begin());
    if (auto mem = mempool.get(id); mem.has_value()) {
      return ok(req.id, {{"transaction_hex", to_hex_local(serialize_transaction(mem->tx))},
                         {"confirmations", "0"}, {"in_mempool", "true"}});
    }
    auto found = find_tx(chain, id);
    if (!found.ok()) return err(req.id, RpcErrorCode::kNotFound, found.error().to_string());
    auto tip = chain.tip();
    const std::uint64_t conf = (tip.ok() && tip.value().has_value() && tip.value()->height >= found.value().height)
                                   ? (static_cast<std::uint64_t>(tip.value()->height - found.value().height) + 1)
                                   : 0;
    return ok(req.id, {{"transaction_hex", to_hex_local(serialize_transaction(found.value().tx))},
                       {"confirmations", std::to_string(conf)},
                       {"height", std::to_string(found.value().height)},
                       {"in_mempool", "false"}});
  });
  server.register_method("getpeerinfo", [p2p](const RpcRequest& req) {
    if (!p2p) return ok(req.id, {{"peers", "[]"}, {"status", "p2p_disabled"}});
    auto peers = p2p->peers();
    json_t* arr = json_array();
    for (const auto& pi : peers) {
      json_t* o = json_object();
      json_object_set_new(o, "id", json_integer(static_cast<json_int_t>(pi.id)));
      json_object_set_new(o, "host", json_string(pi.host.c_str()));
      json_object_set_new(o, "port", json_integer(pi.port));
      json_object_set_new(o, "inbound", json_boolean(pi.inbound));
      json_object_set_new(o, "state", json_integer(static_cast<int>(pi.state)));
      json_array_append_new(arr, o);
    }
    char* text = json_dumps(arr, JSON_COMPACT);
    std::string out = text ? text : "[]"; if (text) free(text); json_decref(arr);
    return ok(req.id, {{"peers_json", out}, {"count", std::to_string(peers.size())}});
  });
  server.register_method("getconnectioncount", [p2p](const RpcRequest& req) { return ok(req.id, {{"count", p2p ? std::to_string(p2p->peer_count()) : "0"}}); });

  // Wallet-facing
  server.register_method("getnewaddress", [](const RpcRequest& req) {
    // Persist in local datadir keystore when available (exchange-friendly: 1 keystore, many addresses).
    try {
      const auto dd = phantomx::default_datadir(phantomx::Network::kRegtest);
      const auto ks = (dd / "wallet" / "wallet.keystore");
      phantomx::wallet::Wallet w(ks.string(), "");
      (void)w.open();
      if (!w.has_wallet()) {
        auto c = w.create();
        if (!c.ok()) return err(req.id, RpcErrorCode::kInternalError, c.error().to_string());
      }
      const std::string label = req.params.count("label") ? req.params.at("label") : ("addr_" + std::to_string(w.account_labels().size() + 1));
      auto ca = w.create_account(label);
      if (!ca.ok()) return err(req.id, RpcErrorCode::kInternalError, ca.error().to_string());
      return ok(req.id, {{"address", w.address_for(label)}, {"label", label}, {"public_key_hex", w.public_key_hex_for(label)}});
    } catch (const std::exception& ex) {
      return err(req.id, RpcErrorCode::kInternalError, ex.what());
    }
  });

  server.register_method("sendtoaddress", [&chain, &mempool](const RpcRequest& req) {
    (void)chain; (void)mempool;
    auto ait = req.params.find("address");
    auto amt = req.params.find("amount");
    if (ait == req.params.end() || amt == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing address/amount");
    const std::uint64_t amount_atoms = std::stoull(amt->second);
    const std::uint64_t fee_atoms = req.params.count("fee") ? std::stoull(req.params.at("fee")) : 2000ULL;
    try {
      const auto dd = phantomx::default_datadir(phantomx::Network::kRegtest);
      const auto ks = (dd / "wallet" / "wallet.keystore");
      // Use local RPC to broadcast through normal path
      phantomx::wallet::Wallet w(ks.string(), "http://127.0.0.1:8740");
      if (const char* tok = std::getenv("PHANTOMX_RPC_TOKEN"); tok != nullptr && *tok != '\0') w.set_rpc_token(tok);
      (void)w.open();
      if (!w.has_wallet()) return err(req.id, RpcErrorCode::kNotFound, "wallet not found in datadir");
      auto sent = w.send(ait->second, amount_atoms, fee_atoms);
      if (!sent.ok()) return err(req.id, RpcErrorCode::kInternalError, sent.error().to_string());
      return ok(req.id, {{"txid", sent.value()}, {"address", ait->second}, {"amount_atoms", std::to_string(amount_atoms)}, {"fee_atoms", std::to_string(fee_atoms)}});
    } catch (const std::exception& ex) {
      return err(req.id, RpcErrorCode::kInternalError, ex.what());
    }
  });
  server.register_method("getbalance", [&chain](const RpcRequest& req) {
    auto pub = parse_pub(req);
    if (!pub.ok()) return err(req.id, RpcErrorCode::kInvalidParams, pub.error().to_string());
    const auto b = balance_for_pub(chain, pub.value());
    return ok(req.id, {
      {"atoms", std::to_string(b.total())},
      {"spendable_atoms", std::to_string(b.spendable)},
      {"immature_atoms", std::to_string(b.immature)},
    });
  });
  server.register_method("getutxos", [&chain](const RpcRequest& req) {
    auto pub = parse_pub(req);
    if (!pub.ok()) return err(req.id, RpcErrorCode::kInvalidParams, pub.error().to_string());
    return ok(req.id, {{"utxos_json", dump_utxos_json(chain, pub.value())}});
  });
  server.register_method("listtransactions", [&chain, &mempool](const RpcRequest& req) {
    auto pub = parse_pub(req);
    if (!pub.ok()) return err(req.id, RpcErrorCode::kInvalidParams, pub.error().to_string());
    const auto spk = script_pubkey_for_pubkey(pub32(pub.value()));
    // Mempool txs touching this key
    json_t* marr = json_array();
    auto utxos_r = chain.load_utxo_set();
    for (const auto& [_, e] : mempool.entries()) {
      bool touch = false;
      for (const auto& o : e.tx.outputs) if (o.script_pubkey == spk) { touch = true; break; }
      if (!touch && utxos_r.ok()) {
        for (const auto& in : e.tx.inputs) {
          auto u = utxos_r.value().get(in.prevout);
          if (u.has_value() && u->script_pubkey == spk) { touch = true; break; }
        }
      }
      if (!touch) continue;
      json_t* item = json_object();
      json_object_set_new(item, "txid", json_string(to_hex_local(e.id).c_str()));
      json_object_set_new(item, "in_mempool", json_true());
      json_object_set_new(item, "confirmations", json_integer(0));
      json_array_append_new(marr, item);
    }
    char* mtext = json_dumps(marr, JSON_COMPACT);
    std::string mempool_json = mtext != nullptr ? mtext : "[]";
    if (mtext != nullptr) free(mtext);
    json_decref(marr);
    return ok(req.id, {{"utxos_json", dump_utxos_json(chain, pub.value())},
                       {"mempool_txs_json", mempool_json}});
  });
  server.register_method("sendrawtransaction", [&chain, &mempool](const RpcRequest& req) {
    const auto it = req.params.find("hex");
    if (it == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing param hex");
    std::vector<std::uint8_t> raw;
    if (!phantomx::from_hex(it->second, raw)) {
      return err(req.id, RpcErrorCode::kInvalidParams, "invalid hex");
    }
    auto tx = phantomx::deserialize_transaction(raw);
    if (!tx.ok()) return err(req.id, RpcErrorCode::kInvalidParams, tx.error().to_string());
    auto utxos = chain.load_utxo_set();
    if (!utxos.ok()) return err(req.id, RpcErrorCode::kInternalError, utxos.error().to_string());
    auto tipr = chain.tip();
    const std::uint32_t spend_h = (tipr.ok() && tipr.value().has_value()) ? tipr.value()->height + 1 : 0u;
    auto added = mempool.add(tx.value(), utxos.value(), true, spend_h);
    if (!added.ok()) return err(req.id, RpcErrorCode::kInvalidParams, added.error().to_string());
    return ok(req.id, {{"txid", to_hex_local(phantomx::txid(tx.value()))}});
  });
  server.register_method("getrawmempool", [&mempool](const RpcRequest& req) {
    json_t* arr = json_array();
    for (const auto& [_, e] : mempool.entries()) json_array_append_new(arr, json_string(to_hex_local(e.id).c_str()));
    char* text = json_dumps(arr, JSON_COMPACT);
    std::string out = text != nullptr ? text : "[]";
    if (text != nullptr) free(text);
    json_decref(arr);
    return ok(req.id, {{"txids_json", out}, {"size", std::to_string(mempool.size())}});
  });
  server.register_method("getmempoolinfo", [&mempool](const RpcRequest& req) {
    return ok(req.id, {{"size", std::to_string(mempool.size())}});
  });
  // --- Verifier registry (stored in chain metadata for node sync) ---
  auto load_verifiers = [&chain]() -> json_t* {
    auto m = chain.get_metadata("verifier_registry_json");
    if (m.ok() && m.value().has_value()) {
      json_error_t je{};
      json_t* arr = json_loads(m.value()->c_str(), 0, &je);
      if (arr && json_is_array(arr)) return arr;
      if (arr) json_decref(arr);
    }
    return json_array();
  };
  auto save_verifiers = [&chain](json_t* arr) -> bool {
    char* text = json_dumps(arr, JSON_COMPACT);
    if (!text) return false;
    auto r = chain.set_metadata("verifier_registry_json", text);
    free(text);
    return r.ok();
  };
  auto load_poi_participants = [&chain]() {
    std::scoped_lock lk(g_poi_mu);
    g_poi_participants.clear();
    auto m = chain.get_metadata("poi_participants_json");
    if (!m.ok() || !m.value().has_value()) return;
    json_error_t je{};
    json_t* root = json_loads(m.value()->c_str(), 0, &je);
    if (!root) return;
    json_t* arr = root;
    if (json_is_object(root)) {
      g_poi_running.store(json_is_true(json_object_get(root, "running")));
      json_t* bw = json_object_get(root, "bandwidth_bytes");
      if (bw && json_is_integer(bw) && json_integer_value(bw) > 0) g_poi_bandwidth_bytes.store(static_cast<std::uint64_t>(json_integer_value(bw)));
      const char* unit = json_string_value(json_object_get(root, "bandwidth_unit"));
      if (unit && *unit) g_poi_bandwidth_unit = unit;
      const char* ra = json_string_value(json_object_get(root, "reward_address"));
      if (ra) g_poi_reward_address = ra;
      json_t* bf = json_object_get(root, "blocks_found");
      if (bf && json_is_integer(bf)) g_poi_blocks_found.store(static_cast<std::uint64_t>(json_integer_value(bf)));
      json_t* lba = json_object_get(root, "last_block_at");
      if (lba && json_is_integer(lba)) g_poi_last_block_at.store(static_cast<std::uint64_t>(json_integer_value(lba)));
      const char* lbh = json_string_value(json_object_get(root, "last_block_hash"));
      if (lbh) g_poi_last_block_hash = lbh;
      json_t* fh = json_object_get(root, "finalized_height");
      if (fh && json_is_integer(fh)) g_poi_finalized_height.store(static_cast<std::uint64_t>(json_integer_value(fh)));
      const char* fhh = json_string_value(json_object_get(root, "finalized_hash"));
      if (fhh) g_poi_finalized_hash = fhh;
      json_t* fq = json_object_get(root, "finality_quorum");
      if (fq && json_is_integer(fq) && json_integer_value(fq) > 0) g_poi_finality_quorum.store(static_cast<std::uint64_t>(json_integer_value(fq)));
      json_t* qmin = json_object_get(root, "q_min");
      if (qmin && json_is_integer(qmin) && json_integer_value(qmin) > 0) g_poi_q_min.store(static_cast<std::uint64_t>(json_integer_value(qmin)));
      arr = json_object_get(root, "participants");
      if (!arr || !json_is_array(arr)) { json_decref(root); return; }
    }
    if (!json_is_array(arr)) { json_decref(root); return; }
    size_t i; json_t* v;
    json_array_foreach(arr, i, v) {
      if (!json_is_object(v)) continue;
      PoiParticipant p;
      const char* id = json_string_value(json_object_get(v, "id"));
      if (!id || !*id) continue;
      p.id = id;
      const char* oa = json_string_value(json_object_get(v, "owner_address"));
      p.owner_address = oa ? oa : id;
      const char* mid = json_string_value(json_object_get(v, "machine_id"));
      p.machine_id = mid ? mid : id;
      const char* sip = json_string_value(json_object_get(v, "source_ip"));
      if (sip) p.source_ip = sip;
      p.bandwidth_bytes = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "bandwidth_bytes")));
      const char* unit = json_string_value(json_object_get(v, "bandwidth_unit"));
      p.bandwidth_unit = unit ? unit : "MB";
      p.last_heartbeat = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "last_heartbeat")));
      p.running = json_is_true(json_object_get(v, "running"));
      json_t* lat = json_object_get(v, "latency_ms");
      if (lat && json_is_number(lat)) p.latency_ms = json_number_value(lat);
      json_t* rel = json_object_get(v, "relay_ratio");
      if (rel && json_is_number(rel)) p.relay_ratio = json_number_value(rel);
      json_t* uok = json_object_get(v, "uptime_ok");
      if (uok && json_is_integer(uok)) p.uptime_ok = static_cast<std::uint64_t>(json_integer_value(uok));
      json_t* uex = json_object_get(v, "uptime_expected");
      if (uex && json_is_integer(uex)) p.uptime_expected = static_cast<std::uint64_t>(json_integer_value(uex));
      json_t* uws = json_object_get(v, "uptime_window_start");
      if (uws && json_is_integer(uws)) p.uptime_window_start = static_cast<std::uint64_t>(json_integer_value(uws));
      if (p.bandwidth_bytes == 0) p.bandwidth_bytes = 100ULL * 1024ULL * 1024ULL;
      g_poi_participants[p.id] = p;
      if (!p.source_ip.empty() && p.running) poi_ip_bind(p.source_ip, p.owner_address, p.id, p.last_heartbeat);
    }
    // Reconcile running flag with any active participant
    bool any_running = g_poi_running.load();
    for (const auto& [_, p] : g_poi_participants) if (p.running) any_running = true;
    g_poi_running.store(any_running);
    json_decref(root);
  };
  auto save_poi_state = [&chain]() {
    json_t* root = json_object();
    json_t* participants = json_array();
    json_t* blocks = json_array();
    {
      std::scoped_lock lk(g_poi_mu);
      json_object_set_new(root, "running", g_poi_running.load() ? json_true() : json_false());
      json_object_set_new(root, "bandwidth_bytes", json_integer(static_cast<json_int_t>(g_poi_bandwidth_bytes.load())));
      json_object_set_new(root, "bandwidth_unit", json_string(g_poi_bandwidth_unit.c_str()));
      json_object_set_new(root, "reward_address", json_string(g_poi_reward_address.c_str()));
      json_object_set_new(root, "blocks_found", json_integer(static_cast<json_int_t>(g_poi_blocks_found.load())));
      json_object_set_new(root, "last_block_at", json_integer(static_cast<json_int_t>(g_poi_last_block_at.load())));
      json_object_set_new(root, "last_block_hash", json_string(g_poi_last_block_hash.c_str()));
      json_object_set_new(root, "finalized_height", json_integer(static_cast<json_int_t>(g_poi_finalized_height.load())));
      json_object_set_new(root, "finalized_hash", json_string(g_poi_finalized_hash.c_str()));
      json_object_set_new(root, "finality_quorum", json_integer(static_cast<json_int_t>(g_poi_finality_quorum.load())));
      json_object_set_new(root, "q_min", json_integer(static_cast<json_int_t>(g_poi_q_min.load())));
      for (const auto& [_, p] : g_poi_participants) {
        json_t* o = json_object();
        json_object_set_new(o, "id", json_string(p.id.c_str()));
        json_object_set_new(o, "owner_address", json_string(p.owner_address.c_str()));
        json_object_set_new(o, "machine_id", json_string(p.machine_id.c_str()));
        json_object_set_new(o, "source_ip", json_string(p.source_ip.c_str()));
        json_object_set_new(o, "bandwidth_bytes", json_integer(static_cast<json_int_t>(p.bandwidth_bytes)));
        json_object_set_new(o, "bandwidth_unit", json_string(p.bandwidth_unit.c_str()));
        json_object_set_new(o, "last_heartbeat", json_integer(static_cast<json_int_t>(p.last_heartbeat)));
        json_object_set_new(o, "running", p.running ? json_true() : json_false());
        if (p.latency_ms >= 0.0) json_object_set_new(o, "latency_ms", json_real(p.latency_ms));
        if (p.relay_ratio >= 0.0) json_object_set_new(o, "relay_ratio", json_real(p.relay_ratio));
        json_object_set_new(o, "uptime_ok", json_integer(static_cast<json_int_t>(p.uptime_ok)));
        json_object_set_new(o, "uptime_expected", json_integer(static_cast<json_int_t>(p.uptime_expected)));
        json_object_set_new(o, "uptime_window_start", json_integer(static_cast<json_int_t>(p.uptime_window_start)));
        json_array_append_new(participants, o);
      }
      std::vector<PoiBlockFinality> bfs;
      bfs.reserve(g_poi_block_finality.size());
      for (const auto& [_, bf] : g_poi_block_finality) bfs.push_back(bf);
      std::sort(bfs.begin(), bfs.end(), [](const auto& a, const auto& b){ return a.height > b.height; });
      if (bfs.size() > 256) bfs.resize(256);
      for (const auto& bf : bfs) {
        json_t* o = json_object();
        json_object_set_new(o, "block_hash", json_string(bf.block_hash.c_str()));
        json_object_set_new(o, "height", json_integer(static_cast<json_int_t>(bf.height)));
        json_object_set_new(o, "quorum_required", json_integer(static_cast<json_int_t>(bf.quorum_required)));
        json_object_set_new(o, "finalized", bf.finalized ? json_true() : json_false());
        json_object_set_new(o, "finalized_at", json_integer(static_cast<json_int_t>(bf.finalized_at)));
        json_t* atts = json_array();
        for (const auto& a : bf.attestations) {
          json_t* ao = json_object();
          json_object_set_new(ao, "verifier_pubkey", json_string(a.verifier_pubkey_hex.c_str()));
          json_object_set_new(ao, "signature", json_string(a.signature_hex.c_str()));
          json_object_set_new(ao, "timestamp", json_integer(static_cast<json_int_t>(a.timestamp)));
          json_array_append_new(atts, ao);
        }
        json_object_set_new(o, "attestations", atts);
        json_array_append_new(blocks, o);
      }
    }
    json_t* idbind = json_array();
    for (const auto& [_, b] : g_poi_identity_by_owner) {
      json_t* o = json_object();
      json_object_set_new(o, "owner_address", json_string(b.owner_address.c_str()));
      json_object_set_new(o, "identity_pubkey_hex", json_string(b.identity_pubkey_hex.c_str()));
      json_object_set_new(o, "first_seen", json_integer(static_cast<json_int_t>(b.first_seen)));
      json_object_set_new(o, "last_seen", json_integer(static_cast<json_int_t>(b.last_seen)));
      json_array_append_new(idbind, o);
    }
    json_object_set_new(root, "participants", participants);
    json_object_set_new(root, "blocks", blocks);
    json_object_set_new(root, "identity_bindings", idbind);
    char* text = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    if (!text) return false;
    std::scoped_lock persist_lk(g_poi_persist_mu);
    auto r = chain.set_metadata("poi_state_json", text);
    free(text);
    return r.ok();
  };
  auto save_poi_participants = [save_poi_state]() { return save_poi_state(); };
  auto save_poi_attestations = save_poi_state;
  // Load combined state; fall back to legacy keys.
  auto load_poi_state = [&chain]() {
    auto m = chain.get_metadata("poi_state_json");
    if (!m.ok() || !m.value().has_value()) return false;
    json_error_t je{};
    json_t* root = json_loads(m.value()->c_str(), 0, &je);
    if (!root || !json_is_object(root)) { if (root) json_decref(root); return false; }
    {
      std::scoped_lock lk(g_poi_mu);
      g_poi_running.store(json_is_true(json_object_get(root, "running")));
      json_t* bw = json_object_get(root, "bandwidth_bytes");
      if (bw && json_is_integer(bw) && json_integer_value(bw) > 0) g_poi_bandwidth_bytes.store(static_cast<std::uint64_t>(json_integer_value(bw)));
      const char* unit = json_string_value(json_object_get(root, "bandwidth_unit"));
      if (unit && *unit) g_poi_bandwidth_unit = unit;
      const char* ra = json_string_value(json_object_get(root, "reward_address"));
      if (ra) g_poi_reward_address = ra;
      json_t* bf = json_object_get(root, "blocks_found");
      if (bf && json_is_integer(bf)) g_poi_blocks_found.store(static_cast<std::uint64_t>(json_integer_value(bf)));
      json_t* lba = json_object_get(root, "last_block_at");
      if (lba && json_is_integer(lba)) g_poi_last_block_at.store(static_cast<std::uint64_t>(json_integer_value(lba)));
      const char* lbh = json_string_value(json_object_get(root, "last_block_hash"));
      if (lbh) g_poi_last_block_hash = lbh;
      json_t* fh = json_object_get(root, "finalized_height");
      if (fh && json_is_integer(fh)) g_poi_finalized_height.store(static_cast<std::uint64_t>(json_integer_value(fh)));
      const char* fhh = json_string_value(json_object_get(root, "finalized_hash"));
      if (fhh) g_poi_finalized_hash = fhh;
      json_t* fq = json_object_get(root, "finality_quorum");
      if (fq && json_is_integer(fq) && json_integer_value(fq) > 0) g_poi_finality_quorum.store(static_cast<std::uint64_t>(json_integer_value(fq)));
      json_t* qmin = json_object_get(root, "q_min");
      if (qmin && json_is_integer(qmin) && json_integer_value(qmin) > 0) g_poi_q_min.store(static_cast<std::uint64_t>(json_integer_value(qmin)));
      g_poi_participants.clear();
      json_t* arr = json_object_get(root, "participants");
      if (arr && json_is_array(arr)) {
        size_t i; json_t* v;
        json_array_foreach(arr, i, v) {
          if (!json_is_object(v)) continue;
          PoiParticipant p;
          const char* id = json_string_value(json_object_get(v, "id"));
          if (!id || !*id) continue;
          p.id = id;
          p.bandwidth_bytes = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "bandwidth_bytes")));
          const char* u = json_string_value(json_object_get(v, "bandwidth_unit"));
          p.bandwidth_unit = u ? u : "MB";
          p.last_heartbeat = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "last_heartbeat")));
          p.running = json_is_true(json_object_get(v, "running"));
          json_t* lat = json_object_get(v, "latency_ms");
          if (lat && json_is_number(lat)) p.latency_ms = json_number_value(lat);
          json_t* rel = json_object_get(v, "relay_ratio");
          if (rel && json_is_number(rel)) p.relay_ratio = json_number_value(rel);
          if (p.bandwidth_bytes == 0) p.bandwidth_bytes = 100ULL * 1024ULL * 1024ULL;
          g_poi_participants[p.id] = p;
        }
      }
      bool any_running = g_poi_running.load();
      for (const auto& [_, p] : g_poi_participants) if (p.running) any_running = true;
      g_poi_running.store(any_running);
      g_poi_block_finality.clear();
      json_t* blocks = json_object_get(root, "blocks");
      if (blocks && json_is_array(blocks)) {
        size_t i; json_t* v;
        json_array_foreach(blocks, i, v) {
          if (!json_is_object(v)) continue;
          PoiBlockFinality bfe;
          const char* bh = json_string_value(json_object_get(v, "block_hash"));
          if (!bh || !*bh) continue;
          bfe.block_hash = bh;
          bfe.height = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "height")));
          bfe.quorum_required = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "quorum_required")));
          if (bfe.quorum_required == 0) bfe.quorum_required = 1;
          bfe.finalized = json_is_true(json_object_get(v, "finalized"));
          bfe.finalized_at = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "finalized_at")));
          json_t* atts = json_object_get(v, "attestations");
          if (atts && json_is_array(atts)) {
            size_t j; json_t* a;
            json_array_foreach(atts, j, a) {
              if (!json_is_object(a)) continue;
              PoiBlockAttestation pa;
              const char* pk = json_string_value(json_object_get(a, "verifier_pubkey"));
              const char* sg = json_string_value(json_object_get(a, "signature"));
              if (pk) pa.verifier_pubkey_hex = pk;
              if (sg) pa.signature_hex = sg;
              pa.timestamp = static_cast<std::uint64_t>(json_integer_value(json_object_get(a, "timestamp")));
              if (!pa.verifier_pubkey_hex.empty()) bfe.attestations.push_back(std::move(pa));
            }
          }
          g_poi_block_finality[bfe.block_hash] = std::move(bfe);
        }
      }
      g_poi_identity_by_owner.clear();
      g_poi_owner_by_identity_pubkey.clear();
      json_t* ib = json_object_get(root, "identity_bindings");
      if (ib && json_is_array(ib)) {
        size_t i; json_t* v;
        json_array_foreach(ib, i, v) {
          if (!json_is_object(v)) continue;
          const char* owner = json_string_value(json_object_get(v, "owner_address"));
          const char* pk = json_string_value(json_object_get(v, "identity_pubkey_hex"));
          if (!owner || !pk || !*owner || !*pk) continue;
          PoiIdentityBinding b;
          b.owner_address = owner; b.identity_pubkey_hex = pk;
          b.first_seen = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "first_seen")));
          b.last_seen = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "last_seen")));
          g_poi_identity_by_owner[b.owner_address] = b;
          g_poi_owner_by_identity_pubkey[b.identity_pubkey_hex] = b.owner_address;
        }
      }
      for (const auto& [_, bfx] : g_poi_block_finality) {
        if (bfx.finalized && bfx.height >= g_poi_finalized_height.load()) {
          g_poi_finalized_height.store(bfx.height);
          g_poi_finalized_hash = bfx.block_hash;
        }
      }
    }
    json_decref(root);
    return true;
  };
  auto load_poi_attestations = [&chain]() {
    std::scoped_lock lk(g_poi_mu);
    g_poi_block_finality.clear();
    auto m = chain.get_metadata("poi_attestations_json");
    if (!m.ok() || !m.value().has_value()) return;
    json_error_t je{};
    json_t* root = json_loads(m.value()->c_str(), 0, &je);
    if (!root || !json_is_object(root)) { if (root) json_decref(root); return; }
    json_t* arr = json_object_get(root, "blocks");
    if (!arr || !json_is_array(arr)) { json_decref(root); return; }
    size_t i; json_t* v;
    json_array_foreach(arr, i, v) {
      if (!json_is_object(v)) continue;
      PoiBlockFinality bf;
      const char* bh = json_string_value(json_object_get(v, "block_hash"));
      if (!bh || !*bh) continue;
      bf.block_hash = bh;
      bf.height = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "height")));
      bf.quorum_required = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "quorum_required")));
      if (bf.quorum_required == 0) bf.quorum_required = 1;
      bf.finalized = json_is_true(json_object_get(v, "finalized"));
      bf.finalized_at = static_cast<std::uint64_t>(json_integer_value(json_object_get(v, "finalized_at")));
      json_t* atts = json_object_get(v, "attestations");
      if (atts && json_is_array(atts)) {
        size_t j; json_t* a;
        json_array_foreach(atts, j, a) {
          if (!json_is_object(a)) continue;
          PoiBlockAttestation pa;
          const char* pk = json_string_value(json_object_get(a, "verifier_pubkey"));
          const char* sg = json_string_value(json_object_get(a, "signature"));
          if (pk) pa.verifier_pubkey_hex = pk;
          if (sg) pa.signature_hex = sg;
          pa.timestamp = static_cast<std::uint64_t>(json_integer_value(json_object_get(a, "timestamp")));
          if (!pa.verifier_pubkey_hex.empty()) bf.attestations.push_back(std::move(pa));
        }
      }
      g_poi_block_finality[bf.block_hash] = std::move(bf);
    }
    json_t* qmin = json_object_get(root, "q_min");
    if (qmin && json_is_integer(qmin) && json_integer_value(qmin) > 0) g_poi_q_min.store(static_cast<std::uint64_t>(json_integer_value(qmin)));
    json_t* fhj = json_object_get(root, "finalized_height");
    if (fhj && json_is_integer(fhj)) g_poi_finalized_height.store(std::max(g_poi_finalized_height.load(), static_cast<std::uint64_t>(json_integer_value(fhj))));
    const char* fhh = json_string_value(json_object_get(root, "finalized_hash"));
    if (fhh && *fhh) g_poi_finalized_hash = fhh;
    for (const auto& [_, bfx] : g_poi_block_finality) {
      if (bfx.finalized && bfx.height >= g_poi_finalized_height.load()) {
        g_poi_finalized_height.store(bfx.height);
        g_poi_finalized_hash = bfx.block_hash;
      }
    }
    json_decref(root);
  };
  if (!load_poi_state()) { load_poi_participants(); load_poi_attestations(); }
  auto verifier_online = [](json_t* v) -> bool {
    const char* st = json_string_value(json_object_get(v, "status"));
    const std::string status = st ? st : "ACTIVE";
    if (status != "ACTIVE" && status != "online") return false;
    json_t* hb = json_object_get(v, "last_heartbeat");
    if (!hb || !json_is_integer(hb)) return false;
    const auto now_ts = static_cast<json_int_t>(std::time(nullptr));
    return (now_ts - json_integer_value(hb)) <= 90;
  };
  auto count_online_verifiers = [load_verifiers, verifier_online]() -> std::uint64_t {
    json_t* arr = load_verifiers();
    std::uint64_t n = 0;
    size_t i; json_t* v;
    json_array_foreach(arr, i, v) { if (verifier_online(v)) ++n; }
    json_decref(arr);
    return n;
  };
  auto poi_required_quorum = [](std::uint64_t online) -> std::uint64_t {
    const std::uint64_t qmin = std::max<std::uint64_t>(1, g_poi_q_min.load());
    if (online == 0) return qmin;
    const std::uint64_t two_thirds = (online * 2 + 2) / 3; // ceil(2/3*N)
    return std::max(qmin, two_thirds);
  };
  auto poi_attestation_message_v1 = [](const std::string& block_hash_hex, std::uint64_t height, const std::string& verifier_pubkey_hex, std::uint64_t ts) {
    return std::string("PHX_POI_ATTEST_V1|") + block_hash_hex + "|" + std::to_string(height) + "|" + verifier_pubkey_hex + "|" + std::to_string(ts);
  };
  auto poi_attestation_message_v2 = [network](const std::string& block_hash_hex, std::uint64_t height, const std::string& verifier_pubkey_hex, std::uint64_t ts, std::uint64_t epoch) {
    const std::string net(phantomx::network_name(network));
    return std::string("PHX/poi/attest/v2|") + net + "|1|" + std::to_string(epoch) + "|" + block_hash_hex + "|" + std::to_string(height) + "|" + verifier_pubkey_hex + "|" + std::to_string(ts);
  };
  [[maybe_unused]] auto poi_attestation_replay_key = [](const std::string& block_hash_hex, const std::string& verifier_pubkey_hex, std::uint64_t epoch) {
    return block_hash_hex + "|" + verifier_pubkey_hex + "|" + std::to_string(epoch);
  };
  auto poi_verify_attestation_sig = [&](const std::string& verifier_pubkey_hex, const std::string& signature_hex, const std::string& msg) -> bool {
    std::vector<std::uint8_t> pkb, sigb;
    if (!phantomx::from_hex(verifier_pubkey_hex, pkb) || !phantomx::from_hex(signature_hex, sigb)) return false;
    if (pkb.size() != 32 || sigb.size() != 64) return false;
    phantomx::crypto::PublicKey pk{};
    std::copy(pkb.begin(), pkb.end(), pk.bytes.begin());
    phantomx::crypto::Signature sig{};
    std::copy(sigb.begin(), sigb.end(), sig.bytes.begin());
    return phantomx::crypto::verify(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size()), sig, pk);
  };
  [[maybe_unused]] auto poi_record_block = [&](std::uint64_t height, const std::string& block_hash_hex) {
    // PTM-001/PTM-006: block recording tracks quorum policy but does not finalize without
    // validated attestations. Bootstrap auto-finalization is disabled by default; it can only
    // be explicitly enabled for isolated dev/regtest via PHANTOMX_POI_BOOTSTRAP_ALLOW=1.
    const auto online = g_poi_verifiers_online.load();
    const auto quorum = poi_required_quorum(online);
    std::scoped_lock lk(g_poi_mu);
    auto& bf = g_poi_block_finality[block_hash_hex];
    bf.height = height;
    bf.block_hash = block_hash_hex;
    bf.quorum_required = quorum;
    const char* bootstrap_allow = std::getenv("PHANTOMX_POI_BOOTSTRAP_ALLOW");
    const bool bootstrap_ok = bootstrap_allow && std::string(bootstrap_allow) == "1";
    if (!bf.finalized && bootstrap_ok && online == 0 && g_poi_q_min.load() <= 1) {
      bf.finalized = true;
      bf.finalized_at = static_cast<std::uint64_t>(std::time(nullptr));
      g_poi_finalized_height.store(height);
      g_poi_finalized_hash = block_hash_hex;
      (void)chain.set_metadata("finalized_height", std::to_string(height));
      (void)chain.set_metadata("finalized_hash", block_hash_hex);
    }
    g_poi_finality_quorum.store(quorum);
  };
  [[maybe_unused]] auto poi_try_finalize = [&](const std::string& block_hash_hex) -> bool {
    std::scoped_lock lk(g_poi_mu);
    auto it = g_poi_block_finality.find(block_hash_hex);
    if (it == g_poi_block_finality.end()) return false;
    auto& bf = it->second;
    if (bf.finalized) return true;
    if (bf.attestations.size() >= bf.quorum_required) {
      bf.finalized = true;
      bf.finalized_at = static_cast<std::uint64_t>(std::time(nullptr));
      g_poi_finalized_height.store(bf.height);
      g_poi_finalized_hash = bf.block_hash;
      (void)chain.set_metadata("finalized_height", std::to_string(bf.height));
      (void)chain.set_metadata("finalized_hash", bf.block_hash);
      return true;
    }
    return false;
  };
  // Deterministic selection for consensus/coinbase: all registered verifiers by height.
  auto pick_rolling_verifier = [load_verifiers](std::uint64_t height) -> json_t* {
    json_t* arr = load_verifiers();
    std::vector<json_t*> all;
    size_t i; json_t* v;
    json_array_foreach(arr, i, v) { all.push_back(v); }
    if (all.empty()) { json_decref(arr); return nullptr; }
    const size_t idx = static_cast<size_t>(height % all.size());
    json_t* picked = json_deep_copy(all[idx]);
    json_object_set_new(picked, "rolling_index", json_integer(static_cast<json_int_t>(idx)));
    json_object_set_new(picked, "rolling_total", json_integer(static_cast<json_int_t>(all.size())));
    json_object_set_new(picked, "block_height", json_integer(static_cast<json_int_t>(height)));
    json_decref(arr);
    return picked; // caller owns
  };

  // Phase 15C.7: production PoP v1 helpers (shared header, no test mirror).
  auto pop_network_u8 = [](phantomx::Network n)->std::uint8_t { return phantomx::poi::pop_network_u8(n); };
  auto encode_pop_payload_v1 = [](const phantomx::poi::PopPayloadFieldsV1& f, std::vector<std::uint8_t>& out)->bool { return phantomx::poi::encode_pop_payload_v1(f, out); };
  auto decode_pop_payload_v1 = [](const std::vector<std::uint8_t>& in, phantomx::poi::PopPayloadFieldsV1& f)->bool { return phantomx::poi::decode_pop_payload_v1(in, f); };

  static std::mutex g_verifier_challenge_mu;
  [[maybe_unused]] auto verifier_challenge_key = [](const std::string& id){ return std::string("verifier_challenge_") + id; };
  auto verifier_challenge_issue = [&, network, pop_network_u8, encode_pop_payload_v1](const std::string& verifier_id, const std::string& owner, const std::string& pubkey_hex) -> RpcResponse {
    if (verifier_id.empty() || verifier_id.size() > 64) return err("", RpcErrorCode::kInvalidParams, "invalid verifier id");
    if (owner.empty() || owner.size() > 128) return err("", RpcErrorCode::kInvalidParams, "invalid owner");
    std::vector<std::uint8_t> pkb;
    if (!phantomx::from_hex(pubkey_hex, pkb) || pkb.size() != 32) return err("", RpcErrorCode::kInvalidParams, "invalid pubkey");
    const std::uint64_t now_ts = static_cast<std::uint64_t>(std::time(nullptr));
    // Resource protection (Phase 15C.5). Defaults conservative; not a Sybil defense.
    static constexpr std::uint64_t kMaxActiveChallengesGlobal = 4096;
    static constexpr std::uint64_t kMaxActiveChallengesPerIdentity = 8;
    static constexpr std::uint64_t kConsumedRetentionSec = 3600;
    static constexpr std::uint64_t kCleanupBatch = 256;
    std::array<std::uint8_t,32> nonce{};
    std::array<std::uint8_t,32> challenge_raw{};
    auto rnd1 = phantomx::crypto::random_bytes(std::span<std::uint8_t>(nonce.data(), nonce.size()));
    auto rnd2 = phantomx::crypto::random_bytes(std::span<std::uint8_t>(challenge_raw.data(), challenge_raw.size()));
    if (!rnd1.ok() || !rnd2.ok()) return err("", RpcErrorCode::kInternalError, "secure random failure");
    const std::uint64_t exp_ts = now_ts + 300;
    const std::string challenge_id = to_hex_local(std::span<const std::uint8_t>(challenge_raw.data(), challenge_raw.size()));
    const std::string net(phantomx::network_name(network));
    phantomx::poi::PopPayloadFieldsV1 f{};
    f.network_u8 = pop_network_u8(network);
    f.verifier_id = verifier_id;
    std::copy(pkb.begin(), pkb.end(), f.pubkey.begin());
    f.owner = owner;
    f.challenge_id = challenge_raw;
    f.nonce = nonce;
    f.issued_at = now_ts;
    f.expires_at = exp_ts;
    std::vector<std::uint8_t> payload;
    if (!encode_pop_payload_v1(f, payload)) return err("", RpcErrorCode::kInvalidParams, "invalid challenge fields");
    phantomx::blockchain::ChainDatabase::VerifierChallengeRecord rec;
    rec.challenge_id = challenge_id;
    rec.verifier_id = verifier_id;
    rec.pubkey_hex = pubkey_hex;
    rec.owner = owner;
    rec.network = net;
    rec.payload_blob = payload;
    rec.issued_at = now_ts;
    rec.expires_at = exp_ts;
    rec.status = "issued";
    // Phase 15C.8: atomic cleanup+quota+insert in one DB transaction.
    auto pr = chain.put_verifier_challenge_with_quota(rec, now_ts, kMaxActiveChallengesGlobal, kMaxActiveChallengesPerIdentity, kConsumedRetentionSec, kCleanupBatch);
    if (!pr.ok()) {
      const std::string em = pr.error().message();
      if (em == "challenge_capacity_global") return err("", RpcErrorCode::kInternalError, "challenge capacity reached");
      if (em == "challenge_capacity_identity") return err("", RpcErrorCode::kRateLimited, "challenge identity limit reached");
      return err("", RpcErrorCode::kInternalError, "challenge persist failed: " + pr.error().to_string());
    }
    RpcResponse resp; resp.ok = true; resp.id = "1";
    resp.result["challenge_id"] = challenge_id; resp.result["domain"] = "PHXVERPOP\1"; resp.result["network"] = net;
    resp.result["format_version"] = "1"; resp.result["protocol_version"] = "1"; resp.result["operation"] = "register";
    resp.result["verifier_id"] = verifier_id; resp.result["pubkey"] = pubkey_hex; resp.result["owner"] = owner;
    resp.result["nonce_hex"] = to_hex_local(std::span<const std::uint8_t>(nonce.data(), nonce.size()));
    resp.result["issued_at"] = std::to_string(now_ts); resp.result["expires_at"] = std::to_string(exp_ts);
    resp.result["payload_hex"] = to_hex_local(std::span<const std::uint8_t>(payload.data(), payload.size()));
    resp.result["algorithm"] = "ed25519";
    return resp;
  };

  server.register_method("verifier_register_challenge", [verifier_challenge_issue](const RpcRequest& req) {
    auto idIt=req.params.find("id"); auto pkIt=req.params.find("pubkey"); auto owIt=req.params.find("owner");
    if (idIt==req.params.end() || pkIt==req.params.end() || owIt==req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing id/pubkey/owner");
    auto r = verifier_challenge_issue(idIt->second, owIt->second, pkIt->second);
    r.id = req.id; return r;
  });

  server.register_method("verifier_register", [&chain, load_verifiers, save_verifiers, network, decode_pop_payload_v1, pop_network_u8](const RpcRequest& req) {
    auto idIt = req.params.find("id");
    auto addrIt = req.params.find("address");
    auto epIt = req.params.find("endpoint");
    auto pkIt = req.params.find("pubkey");
    auto ownerIt = req.params.find("owner");
    auto chIt = req.params.find("challenge_id");
    auto sigIt = req.params.find("pop_sig");
    if (idIt == req.params.end() || addrIt == req.params.end() || epIt == req.params.end() || pkIt == req.params.end() || ownerIt == req.params.end() || chIt == req.params.end() || sigIt == req.params.end())
      return err(req.id, RpcErrorCode::kInvalidParams, "missing id/address/endpoint/pubkey/owner/challenge_id/pop_sig");
    if (idIt->second.empty() || idIt->second.size() > 64) return err(req.id, RpcErrorCode::kInvalidParams, "invalid id");
    if (epIt->second.empty() || epIt->second.size() > 256 || epIt->second.find('/') != std::string::npos)
      return err(req.id, RpcErrorCode::kInvalidParams, "invalid endpoint");
    const char* reg_token_env = std::getenv("PHANTOMX_VERIFIER_REG_TOKEN");
    if (reg_token_env && *reg_token_env) {
      auto rt = req.params.find("reg_token");
      if (rt == req.params.end() || rt->second != reg_token_env) return err(req.id, RpcErrorCode::kForbidden, "invalid verifier reg_token");
    }
    const auto hrp = phantomx::address_hrp(network);
    auto addr_check = phantomx::script_pubkey_for_address(addrIt->second, hrp);
    if (!addr_check.ok())
      return err(req.id, RpcErrorCode::kInvalidParams, "invalid address for network: " + addr_check.error().to_string());

    std::vector<std::uint8_t> pkb;
    if (!phantomx::from_hex(pkIt->second, pkb) || pkb.size() != 32)
      return err(req.id, RpcErrorCode::kInvalidParams, "invalid pubkey (expected 32-byte hex)");
    auto pk_parsed = phantomx::crypto::parse_public_key_hex(pkIt->second);
    auto sig_parsed = phantomx::crypto::parse_signature_hex(sigIt->second);
    if (!pk_parsed.ok() || !sig_parsed.ok()) return err(req.id, RpcErrorCode::kInvalidParams, "invalid pubkey/signature encoding");

    // Load challenge and verify PoP, then atomically consume in DB.
    auto cr = chain.get_verifier_challenge(chIt->second);
    if (!cr.ok()) return err(req.id, RpcErrorCode::kInternalError, "challenge lookup failed");
    if (!cr.value().has_value()) return err(req.id, RpcErrorCode::kForbidden, "unknown or expired challenge");
    const auto& rec = cr.value().value();
    const std::uint64_t now_ts = static_cast<std::uint64_t>(std::time(nullptr));
    if (rec.used_at.has_value() || rec.status == "used") return err(req.id, RpcErrorCode::kForbidden, "challenge already consumed");
    if (rec.expires_at == 0 || now_ts > rec.expires_at) return err(req.id, RpcErrorCode::kForbidden, "challenge expired");
    const std::string net_expected(phantomx::network_name(network));
    if (rec.network != net_expected) return err(req.id, RpcErrorCode::kForbidden, "challenge network mismatch");
    if (rec.verifier_id != idIt->second || rec.pubkey_hex != pkIt->second || rec.owner != ownerIt->second)
      return err(req.id, RpcErrorCode::kForbidden, "challenge binding mismatch");
    // Strict canonical decode/validation before verify
    phantomx::poi::PopPayloadFieldsV1 pf{};
    if (!decode_pop_payload_v1(rec.payload_blob, pf)) return err(req.id, RpcErrorCode::kForbidden, "invalid challenge payload format");
    const std::uint8_t net_u8_expected = pop_network_u8(network);
    if (pf.network_u8 != net_u8_expected) return err(req.id, RpcErrorCode::kForbidden, "challenge network mismatch");
    if (pf.verifier_id != rec.verifier_id || pf.owner != rec.owner) return err(req.id, RpcErrorCode::kForbidden, "challenge binding mismatch");
    if (to_hex_local(std::span<const std::uint8_t>(pf.challenge_id.data(), pf.challenge_id.size())) != rec.challenge_id) return err(req.id, RpcErrorCode::kForbidden, "challenge id mismatch");
    if (to_hex_local(std::span<const std::uint8_t>(pf.pubkey.data(), pf.pubkey.size())) != rec.pubkey_hex) return err(req.id, RpcErrorCode::kForbidden, "challenge pubkey mismatch");
    if (pf.issued_at != rec.issued_at || pf.expires_at != rec.expires_at) return err(req.id, RpcErrorCode::kForbidden, "challenge timestamp mismatch");
    const std::span<const std::uint8_t> msg(rec.payload_blob.data(), rec.payload_blob.size());
    if (!phantomx::crypto::verify(msg, sig_parsed.value(), pk_parsed.value()))
      return err(req.id, RpcErrorCode::kForbidden, "invalid proof-of-possession signature");
    auto consumed = chain.consume_verifier_challenge(chIt->second, idIt->second, pkIt->second, ownerIt->second, net_expected, now_ts);
    if (!consumed.ok()) return err(req.id, RpcErrorCode::kInternalError, "challenge consume failed");
    if (!consumed.value()) return err(req.id, RpcErrorCode::kForbidden, "challenge replay/consume conflict");

    json_t* arr = load_verifiers();
    json_t* found = nullptr;
    size_t i; json_t* v;
    json_array_foreach(arr, i, v) {
      const char* vid = json_string_value(json_object_get(v, "id"));
      const char* vad = json_string_value(json_object_get(v, "address"));
      const char* vpk = json_string_value(json_object_get(v, "pubkey"));
      const char* vow = json_string_value(json_object_get(v, "owner"));
      if ((vid && idIt->second == vid) || (vad && addrIt->second == vad) || (vpk && pkIt->second == vpk)) {
        // Allow exact same identity record; reject conflicts/rebinding.
        const bool same_id = vid && idIt->second == vid;
        const bool same_addr = vad && addrIt->second == vad;
        const bool same_pk = vpk && pkIt->second == vpk;
        const bool same_owner = (!vow || ownerIt->second == vow);
        if (!(same_id && same_addr && same_pk && same_owner)) {
          json_decref(arr);
          return err(req.id, RpcErrorCode::kForbidden, "verifier identity conflict/rebinding rejected");
        }
        found = v; break;
      }
    }
    if (!found) {
      found = json_object();
      json_object_set_new(found, "id", json_string(idIt->second.c_str()));
      json_object_set_new(found, "registered_at", json_integer(static_cast<json_int_t>(now_ts)));
      json_object_set_new(found, "status", json_string("PENDING")); // safe default; activation requires explicit authorized policy
      json_array_append_new(arr, found);
      found = json_array_get(arr, json_array_size(arr) - 1);
    }
    json_object_set_new(found, "id", json_string(idIt->second.c_str()));
    json_object_set_new(found, "address", json_string(addrIt->second.c_str()));
    json_object_set_new(found, "endpoint", json_string(epIt->second.c_str()));
    json_object_set_new(found, "pubkey", json_string(pkIt->second.c_str()));
    json_object_set_new(found, "owner", json_string(ownerIt->second.c_str()));
    json_object_set_new(found, "pop_verified", json_true());
    json_object_set_new(found, "pop_challenge_id", json_string(chIt->second.c_str()));
    json_object_set_new(found, "pop_verified_at", json_integer(static_cast<json_int_t>(now_ts)));
    json_object_set_new(found, "network", json_string(std::string(phantomx::network_name(network)).c_str()));
    auto nameIt = req.params.find("name");
    if (nameIt != req.params.end()) json_object_set_new(found, "name", json_string(nameIt->second.c_str()));
    else if (!json_object_get(found, "name")) json_object_set_new(found, "name", json_string(idIt->second.c_str()));
    auto apiIt = req.params.find("api_url");
    if (apiIt != req.params.end()) json_object_set_new(found, "api_url", json_string(apiIt->second.c_str()));
    const char* st = json_string_value(json_object_get(found, "status"));
    if (!st || !*st) json_object_set_new(found, "status", json_string("ACTIVE"));
    json_object_set_new(found, "updated_at", json_integer(static_cast<json_int_t>(now_ts)));
    bool ok_save = save_verifiers(arr);
    char* text = json_dumps(found, JSON_COMPACT);
    std::string vjson = text ? text : "{}";
    if (text) free(text);
    json_decref(arr);
    if (!ok_save) return err(req.id, RpcErrorCode::kInternalError, "failed to save verifier registry");
    return ok(req.id, {{"status", "registered"}, {"verifier_json", vjson}, {"address_validated", "true"}, {"pop_verified","true"}});
  });

  server.register_method("verifier_heartbeat", [load_verifiers, save_verifiers, verifier_online](const RpcRequest& req) {
    auto idIt = req.params.find("id");
    auto addrIt = req.params.find("address");
    if (idIt == req.params.end() && addrIt == req.params.end())
      return err(req.id, RpcErrorCode::kInvalidParams, "missing id/address");
    const char* reg_token_env = std::getenv("PHANTOMX_VERIFIER_REG_TOKEN");
    if (reg_token_env && *reg_token_env) {
      auto rt = req.params.find("reg_token");
      if (rt == req.params.end() || rt->second != reg_token_env) return err(req.id, RpcErrorCode::kForbidden, "invalid verifier reg_token");
    }
    json_t* arr = load_verifiers();
    json_t* found = nullptr;
    size_t i; json_t* v;
    json_array_foreach(arr, i, v) {
      const char* vid = json_string_value(json_object_get(v, "id"));
      const char* vad = json_string_value(json_object_get(v, "address"));
      if ((idIt != req.params.end() && vid && idIt->second == vid) ||
          (addrIt != req.params.end() && vad && addrIt->second == vad)) { found = v; break; }
    }
    if (!found) { json_decref(arr); return err(req.id, RpcErrorCode::kNotFound, "verifier not registered"); }
    const auto now_ts = static_cast<json_int_t>(std::time(nullptr));
    const char* st = json_string_value(json_object_get(found, "status"));
    const std::string status = st ? st : "ACTIVE";
    if (status != "ACTIVE" && status != "online") {
      json_decref(arr);
      return err(req.id, RpcErrorCode::kForbidden, "verifier not eligible for heartbeat");
    }
    json_object_set_new(found, "last_heartbeat", json_integer(now_ts));
    json_object_set_new(found, "status", json_string("ACTIVE"));
    auto epIt = req.params.find("endpoint"); if (epIt != req.params.end()) json_object_set_new(found, "endpoint", json_string(epIt->second.c_str()));
    bool ok_save = save_verifiers(arr);
    char* text = json_dumps(found, JSON_COMPACT);
    std::string vjson = text ? text : "{}";
    if (text) free(text);
    // PTM-001: heartbeat updates liveness signal only. It MUST NOT recalculate quorum policy
    // or finalize pending blocks. Finality is authorized only via validated attestation path.
    {
      std::uint64_t on=0; size_t ii; json_t* vv;
      json_array_foreach(arr,ii,vv){ if(verifier_online(vv)) ++on; }
      g_poi_verifiers_online.store(on);
    }
    json_decref(arr);
    if (!ok_save) return err(req.id, RpcErrorCode::kInternalError, "failed to save verifier registry");
    return ok(req.id, {{"status", "ok"}, {"verifier_json", vjson}, {"finality_effect", "none_liveness_only"}});
  });

  server.register_method("verifier_set_status", [load_verifiers, save_verifiers](const RpcRequest& req) {
    const char* admin_env = std::getenv("PHANTOMX_ADMIN_TOKEN");
    auto at = req.params.find("admin_token");
    if (!admin_env || !*admin_env || at == req.params.end() || at->second != admin_env) {
      return err(req.id, RpcErrorCode::kForbidden, "admin authorization required");
    }
    auto idIt = req.params.find("id"); auto stIt = req.params.find("status");
    if (idIt == req.params.end() || stIt == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing id/status");
    const std::string new_st = stIt->second;
    if (new_st!="ACTIVE" && new_st!="SUSPENDED" && new_st!="REVOKED" && new_st!="EXPIRED" && new_st!="PENDING") return err(req.id, RpcErrorCode::kInvalidParams, "invalid status");
    json_t* arr = load_verifiers(); json_t* found=nullptr; size_t i; json_t* v;
    json_array_foreach(arr,i,v){ const char* vid=json_string_value(json_object_get(v,"id")); if(vid && idIt->second==vid){found=v;break;} }
    if(!found){ json_decref(arr); return err(req.id, RpcErrorCode::kNotFound, "verifier not found"); }
    const char* cur_c = json_string_value(json_object_get(found, "status"));
    const std::string cur_st = cur_c ? cur_c : "PENDING";
    auto allowed = [&](const std::string& from, const std::string& to)->bool{
      if (from==to) return true;
      if (to=="ACTIVE") return from=="PENDING" || from=="SUSPENDED"; // explicit admin activation/reinstatement
      if (to=="SUSPENDED") return from=="ACTIVE" || from=="PENDING";
      if (to=="REVOKED") return from=="ACTIVE" || from=="PENDING" || from=="SUSPENDED";
      if (to=="EXPIRED") return from=="ACTIVE" || from=="PENDING" || from=="SUSPENDED";
      if (to=="PENDING") return false; // do not demote to pending
      return false;
    };
    if (!allowed(cur_st, new_st)) { json_decref(arr); return err(req.id, RpcErrorCode::kForbidden, "invalid verifier status transition"); }
    json_object_set_new(found, "status", json_string(new_st.c_str()));
    json_object_set_new(found, "updated_at", json_integer(static_cast<json_int_t>(std::time(nullptr))));
    bool ok_save = save_verifiers(arr);
    char* text = json_dumps(found, JSON_COMPACT); std::string vjson=text?text:"{}"; if(text) free(text); json_decref(arr);
    if(!ok_save) return err(req.id, RpcErrorCode::kInternalError, "failed to save verifier registry");
    return ok(req.id, {{"status","ok"},{"verifier_json",vjson}});
  });

  server.register_method("verifier_list", [load_verifiers, verifier_online](const RpcRequest& req) {
    json_t* arr = load_verifiers();
    json_t* out = json_array();
    const auto now_ts = static_cast<json_int_t>(std::time(nullptr));
    size_t i; json_t* v;
    json_array_foreach(arr, i, v) {
      json_t* item = json_deep_copy(v);
      const bool on = verifier_online(v);
      json_object_set_new(item, "status", json_string(on ? "online" : "offline"));
      json_object_set_new(item, "checked_at", json_integer(now_ts));
      json_array_append_new(out, item);
    }
    const auto count = json_array_size(out);
    char* text = json_dumps(out, JSON_COMPACT);
    std::string vjson = text ? text : "[]";
    if (text) free(text);
    json_decref(out); json_decref(arr);
    return ok(req.id, {{"verifiers_json", vjson}, {"count", std::to_string(count)}});
  });

  server.register_method("poi_submit_attestation", [&, p2p, aggregator_state_path, aggregator_db_path, load_verifiers, save_poi_attestations, poi_attestation_message_v1, poi_attestation_message_v2, poi_verify_attestation_sig](const RpcRequest& req) {
    auto bhIt = req.params.find("block_hash");
    auto pkIt = req.params.find("verifier_pubkey");
    auto sigIt = req.params.find("signature");
    auto hIt = req.params.find("height");
    auto tsIt = req.params.find("timestamp");
    auto epIt = req.params.find("epoch");
    if (bhIt == req.params.end() || pkIt == req.params.end() || sigIt == req.params.end())
      return err(req.id, RpcErrorCode::kInvalidParams, "missing block_hash/verifier_pubkey/signature");
    const std::string block_hash_hex = bhIt->second;
    const std::string verifier_pubkey_hex = pkIt->second;
    const std::string signature_hex = sigIt->second;
    const std::uint64_t height = hIt == req.params.end() ? 0ULL : std::stoull(hIt->second);
    const std::uint64_t ts = tsIt == req.params.end() ? static_cast<std::uint64_t>(std::time(nullptr)) : std::stoull(tsIt->second);
    const bool has_epoch = (epIt != req.params.end());
    const std::uint64_t epoch = has_epoch ? std::stoull(epIt->second) : 0ULL;

    std::vector<std::uint8_t> bhb;
    if (!phantomx::from_hex(block_hash_hex, bhb) || bhb.size() != 32)
      return err(req.id, RpcErrorCode::kInvalidParams, "invalid block_hash");

    const auto now_ts = static_cast<std::uint64_t>(std::time(nullptr));
    if (ts > now_ts + 120) return err(req.id, RpcErrorCode::kInvalidParams, "attestation timestamp in future");
    if (now_ts > ts + kPoiAttestationMaxAgeSec) return err(req.id, RpcErrorCode::kInvalidParams, "stale attestation");

    // Target binding: if block exists locally, require hash/height match.
    phantomx::Hash256Bytes bh_arr{};
    std::copy(bhb.begin(), bhb.end(), bh_arr.begin());
    auto idx = chain.get_block_index(bh_arr);
    if (idx.ok() && idx.value().has_value()) {
      if (idx.value()->height != height) return err(req.id, RpcErrorCode::kInvalidParams, "attestation height mismatch for known block");
    }

    // verify verifier is registered
    bool registered = false;
    bool eligible = false;
    {
      json_t* arr = load_verifiers();
      size_t i; json_t* v;
      json_array_foreach(arr, i, v) {
        const char* pk = json_string_value(json_object_get(v, "pubkey"));
        if (pk && verifier_pubkey_hex == pk) {
          registered = true;
          const char* st = json_string_value(json_object_get(v, "status"));
          const std::string status = st ? st : "ACTIVE";
          eligible = (status == "ACTIVE");
          break;
        }
      }
      json_decref(arr);
    }
    if (!registered) return err(req.id, RpcErrorCode::kForbidden, "verifier pubkey not registered");
    if (!eligible) return err(req.id, RpcErrorCode::kForbidden, "verifier not eligible");

    std::string msg;
    bool legacy_v1 = false;
    if (has_epoch) {
      msg = poi_attestation_message_v2(block_hash_hex, height, verifier_pubkey_hex, ts, epoch);
    } else {
      // Legacy v1 compatibility: accepted only for existing callers; new callers should provide epoch.
      legacy_v1 = true;
      msg = poi_attestation_message_v1(block_hash_hex, height, verifier_pubkey_hex, ts);
    }
    if (!poi_verify_attestation_sig(verifier_pubkey_hex, signature_hex, msg))
      return err(req.id, RpcErrorCode::kInvalidParams, "invalid attestation signature");
    bool duplicate = false;
    bool replay = false;
    bool finalized = false;
    bool too_many = false;
    std::uint64_t quorum_required = 1;
    std::uint64_t att_count = 0;
    {
      std::scoped_lock lk(g_poi_mu);
      auto& bf = g_poi_block_finality[block_hash_hex];
      if (bf.block_hash.empty()) { bf.block_hash = block_hash_hex; bf.height = height; }
      if (bf.height == 0) bf.height = height;
      if (bf.height != height) return err(req.id, RpcErrorCode::kInvalidParams, "attestation height conflicts with existing block finality state");
      if (bf.quorum_required == 0) bf.quorum_required = std::max<std::uint64_t>(1, g_poi_q_min.load());
      for (const auto& a : bf.attestations) {
        if (a.verifier_pubkey_hex == verifier_pubkey_hex) { duplicate = true; break; }
        if (a.signature_hex == signature_hex) { replay = true; break; }
      }
      if (replay) return err(req.id, RpcErrorCode::kInvalidParams, "replay attestation signature");
      if (!duplicate) {
        if (bf.attestations.size() >= kPoiMaxAttestationsPerBlock) {
          too_many = true;
        } else {
          PoiBlockAttestation pa;
          pa.verifier_pubkey_hex = verifier_pubkey_hex;
          pa.signature_hex = signature_hex;
          pa.timestamp = ts;
          bf.attestations.push_back(std::move(pa));
        }
      }
      quorum_required = bf.quorum_required;
      att_count = bf.attestations.size();
      if (!bf.finalized && !too_many && att_count >= quorum_required) {
        bf.finalized = true;
        bf.finalized_at = static_cast<std::uint64_t>(std::time(nullptr));
        g_poi_finalized_height.store(bf.height);
        g_poi_finalized_hash = bf.block_hash;
        (void)chain.set_metadata("finalized_height", std::to_string(bf.height));
        (void)chain.set_metadata("finalized_hash", bf.block_hash);
      }
      finalized = bf.finalized;
    }
    if (too_many) return err(req.id, RpcErrorCode::kInvalidParams, "too many attestations for block");
    save_poi_attestations();
    if (p2p) {
      json_t* gp = json_object();
      json_object_set_new(gp, "type", json_string("poi_attestation"));
      json_object_set_new(gp, "verifier_pubkey", json_string(verifier_pubkey_hex.c_str()));
      json_object_set_new(gp, "block_hash", json_string(block_hash_hex.c_str()));
      json_object_set_new(gp, "height", json_integer(static_cast<json_int_t>(height)));
      json_object_set_new(gp, "timestamp", json_integer(static_cast<json_int_t>(ts)));
      json_object_set_new(gp, "signature", json_string(signature_hex.c_str()));
      if (has_epoch) json_object_set_new(gp, "epoch", json_integer(static_cast<json_int_t>(epoch)));
      json_object_set_new(gp, "domain_version", json_string(has_epoch ? "v2" : "v1"));
      char* gt = json_dumps(gp, JSON_COMPACT);
      if (gt) { (void)p2p->broadcast_poi_attestation(gt); free(gt); }
      json_decref(gp);
    }

    return ok(req.id, {
      {"status", duplicate ? "duplicate" : "accepted"},
      {"block_hash", block_hash_hex},
      {"height", std::to_string(height)},
      {"attestations", std::to_string(att_count)},
      {"quorum_required", std::to_string(quorum_required)},
      {"finalized", finalized ? "true" : "false"},
      {"finality_model", "poi_attested"},
      {"attestation_domain", legacy_v1 ? "v1_legacy" : "v2"}
    });
  });

  if (p2p) {
    p2p->set_poi_attestation_handler([&chain, count_online_verifiers, poi_required_quorum, save_poi_attestations, load_verifiers, poi_attestation_message_v1, poi_attestation_message_v2, poi_verify_attestation_sig](const std::string& payload) {
      json_error_t je{};
      json_t* root = json_loads(payload.c_str(), 0, &je);
      if (!root || !json_is_object(root)) { if (root) json_decref(root); return; }
      auto gs = [&](const char* k) -> std::string { const char* v = json_string_value(json_object_get(root, k)); return v ? std::string(v) : std::string(); };
      auto gi = [&](const char* k) -> std::uint64_t { json_t* v = json_object_get(root, k); return (v && json_is_integer(v)) ? static_cast<std::uint64_t>(json_integer_value(v)) : 0ULL; };
      const std::string verifier_pubkey_hex = gs("verifier_pubkey");
      const std::string block_hash_hex = gs("block_hash");
      const std::string signature_hex = gs("signature");
      const std::uint64_t height = gi("height");
      const std::uint64_t ts = gi("timestamp");
      const bool has_epoch = json_object_get(root, "epoch") != nullptr && json_is_integer(json_object_get(root, "epoch"));
      const std::uint64_t epoch = has_epoch ? gi("epoch") : 0ULL;
      json_decref(root);
      if (verifier_pubkey_hex.empty() || block_hash_hex.empty() || signature_hex.empty()) return;
      std::vector<std::uint8_t> bhb;
      if (!phantomx::from_hex(block_hash_hex, bhb) || bhb.size() != 32) return;
      const auto now_ts = static_cast<std::uint64_t>(std::time(nullptr));
      if (ts > now_ts + 120 || now_ts > ts + kPoiAttestationMaxAgeSec) return;
      phantomx::Hash256Bytes bh_arr{};
      std::copy(bhb.begin(), bhb.end(), bh_arr.begin());
      auto idx = chain.get_block_index(bh_arr);
      if (idx.ok() && idx.value().has_value() && idx.value()->height != height) return;
      bool registered = false; bool eligible = false;
      {
        json_t* arr = load_verifiers();
        size_t i; json_t* v;
        json_array_foreach(arr, i, v) {
          const char* pk = json_string_value(json_object_get(v, "pubkey"));
          if (pk && verifier_pubkey_hex == pk) {
            registered = true;
            const char* st = json_string_value(json_object_get(v, "status"));
            eligible = ((st ? std::string(st) : std::string("ACTIVE")) == "ACTIVE");
            break;
          }
        }
        json_decref(arr);
      }
      if (!registered || !eligible) return;
      const auto msg = has_epoch ? poi_attestation_message_v2(block_hash_hex, height, verifier_pubkey_hex, ts, epoch)
                                 : poi_attestation_message_v1(block_hash_hex, height, verifier_pubkey_hex, ts);
      if (!poi_verify_attestation_sig(verifier_pubkey_hex, signature_hex, msg)) return;
      bool too_many = false;
      {
        std::scoped_lock lk(g_poi_mu);
        const std::uint64_t online = count_online_verifiers();
        auto& bf = g_poi_block_finality[block_hash_hex];
        if (bf.block_hash.empty()) { bf.block_hash = block_hash_hex; bf.height = height; }
        if (bf.height != 0 && bf.height != height) return;
        if (bf.quorum_required == 0) bf.quorum_required = poi_required_quorum(online);
        bool duplicate = false;
        for (const auto& a : bf.attestations) {
          if (a.verifier_pubkey_hex == verifier_pubkey_hex) { duplicate = true; break; }
          if (a.signature_hex == signature_hex) return; // replay
        }
        if (!duplicate) {
          if (bf.attestations.size() >= kPoiMaxAttestationsPerBlock) { too_many = true; }
          else {
            PoiBlockAttestation pa; pa.verifier_pubkey_hex = verifier_pubkey_hex; pa.signature_hex = signature_hex; pa.timestamp = ts;
            bf.attestations.push_back(std::move(pa));
          }
        }
        if (!too_many && !bf.finalized && bf.attestations.size() >= bf.quorum_required) {
          bf.finalized = true; bf.finalized_at = static_cast<std::uint64_t>(std::time(nullptr));
          g_poi_finalized_height.store(bf.height); g_poi_finalized_hash = bf.block_hash;
          (void)chain.set_metadata("finalized_height", std::to_string(bf.height));
          (void)chain.set_metadata("finalized_hash", bf.block_hash);
        }
      }
      if (!too_many) save_poi_attestations();
    });
  }

  server.register_method("setpoiquorum", [save_poi_participants](const RpcRequest& req) {
    // PTM-002: finality-sensitive parameter. Require explicit admin token separate from
    // ordinary RPC token. No silent fallback; fail closed when token not configured.
    const char* admin_env = std::getenv("PHANTOMX_ADMIN_TOKEN");
    if (!admin_env || !*admin_env) {
      return err(req.id, RpcErrorCode::kForbidden, "setpoiquorum disabled: PHANTOMX_ADMIN_TOKEN not configured");
    }
    auto at = req.params.find("admin_token");
    if (at == req.params.end() || at->second != admin_env) {
      return err(req.id, RpcErrorCode::kForbidden, "admin authorization required");
    }
    auto it = req.params.find("q_min");
    if (it == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing q_min");
    std::uint64_t qmin = 1;
    try { qmin = std::stoull(it->second); } catch (...) { return err(req.id, RpcErrorCode::kInvalidParams, "invalid q_min"); }
    if (qmin < 1 || qmin > 256) return err(req.id, RpcErrorCode::kInvalidParams, "q_min out of range (1..256)");
    g_poi_q_min.store(qmin);
    g_poi_finality_quorum.store(std::max(g_poi_finality_quorum.load(), qmin));
    save_poi_participants();
    return ok(req.id, {{"status","ok"},{"q_min",std::to_string(g_poi_q_min.load())},{"finality_quorum",std::to_string(g_poi_finality_quorum.load())},{"authorization","admin_token"}});
  });

  server.register_method("verifier_rolling", [pick_rolling_verifier](const RpcRequest& req) {
    auto hit = req.params.find("height");
    const std::uint64_t h = hit == req.params.end() ? 0ULL : std::stoull(hit->second);
    json_t* picked = pick_rolling_verifier(h);
    if (!picked) return ok(req.id, {{"status", "no_verifiers"}});
    char* text = json_dumps(picked, JSON_COMPACT);
    std::string vjson = text ? text : "{}";
    if (text) free(text);
    json_decref(picked);
    return ok(req.id, {{"verifier_json", vjson}});
  });

  auto mine_one = [&chain, &mempool, pick_rolling_verifier](const std::string& reward_address, bool dev_generate = false) -> Result<Hash256Bytes> {
    auto tip_r = chain.tip();
    if (!tip_r.ok()) return tip_r.error();
    const bool has_tip = tip_r.value().has_value();
    const std::uint32_t next_height = has_tip ? tip_r.value()->height + 1 : 0;
    const Hash256Bytes prev = has_tip ? tip_r.value()->hash : Hash256Bytes{};

    auto script_r = script_pubkey_for_address(reward_address, address_hrp(Network::kRegtest));
    if (!script_r.ok()) return script_r.error();
    // PoI score influences contributor share in PoI mode (deterministic, bounded). 
    // Uptime is self-reported heartbeat accounting; production settlement requires verified proofs.
    double poi_settlement_score = 0.0;
    if (g_poi_running.load()) {
      const auto now_sec = static_cast<std::uint64_t>(std::time(nullptr));
      std::scoped_lock lk(g_poi_mu);
      for (const auto& [_, p] : g_poi_participants) {
        if (!p.running) continue;
        if (p.last_heartbeat == 0 || (now_sec >= p.last_heartbeat && (now_sec - p.last_heartbeat) > kPoiHeartbeatWindowSec)) continue;
        const double up = poi_uptime_ratio(p, now_sec);
        phantomx::poi::PoiCompositeInput in;
        in.bandwidth_mbps = static_cast<std::uint64_t>(std::llround(std::max(0.0, poi_bandwidth_mbps(p.bandwidth_bytes, p.bandwidth_unit))));
        in.bandwidth_valid = in.bandwidth_mbps > 0;
        in.latency_ms = p.latency_ms;
        in.uptime_ratio = up;
        in.relay_ratio = p.relay_ratio;
        in.active_miners = poi_active_miners(now_sec);
        in.network_valid = true;
        auto sr = phantomx::poi::poi_composite_score_v2(in);
        if (sr.available) poi_settlement_score = std::max(poi_settlement_score, sr.score);
      }
    }

    // Collect mempool txs that are still valid against current UTXO.
    auto utxos_r = chain.load_utxo_set();
    if (!utxos_r.ok()) return utxos_r.error();
    UtxoSet view = utxos_r.value();
    std::vector<Transaction> block_txs;
    std::vector<Hash256Bytes> included;
    std::uint64_t fees = 0;

    // Coinbase placeholder (amount filled after fees known)
    Transaction coinbase;
    coinbase.version = kTxVersionV1;
    coinbase.kind = kTxKindCoinbase;
    TxIn cb_in;
    cb_in.prevout.txid = Hash256Bytes{};
    cb_in.prevout.vout = 0xFFFFFFFFu;
    const auto poi_now = static_cast<std::uint64_t>(std::time(nullptr));
    const auto poi_m = poi_active_miners(poi_now);
    const auto poi_e = poi_epoch(poi_m);
    std::string cbmsg = (dev_generate ? "PhantomX-" : "PHXPOI-") + std::to_string(next_height);
    if (g_poi_running.load()) cbmsg += "|POI|e=" + std::to_string(poi_e) + "|m=" + std::to_string(poi_m);
    cb_in.script_sig.assign(cbmsg.begin(), cbmsg.end());
    cb_in.sequence = 0xFFFFFFFFu;
    coinbase.inputs.push_back(cb_in);
    TxOut cb_out;
    cb_out.amount_atoms = 0;
    cb_out.script_pubkey = script_r.value();
    coinbase.outputs.push_back(cb_out);
    coinbase.locktime = 0;
    block_txs.push_back(coinbase);

    // Simple mempool selection: deterministic by txid hex order.
    std::vector<MempoolEntry> candidates;
    candidates.reserve(mempool.size());
    for (const auto& [_, e] : mempool.entries()) candidates.push_back(e);
    std::sort(candidates.begin(), candidates.end(), [](const MempoolEntry& a, const MempoolEntry& b) {
      return to_hex(std::span<const std::uint8_t>(a.id.data(), a.id.size())) <
             to_hex(std::span<const std::uint8_t>(b.id.data(), b.id.size()));
    });
    for (const auto& e : candidates) {
      auto v = validate_transaction(e.tx, view, true, next_height);
      if (!v.ok) continue;
      // apply to view
      for (const auto& in : e.tx.inputs) view.spend(in.prevout);
      const auto tid = txid(e.tx);
      for (std::uint32_t i = 0; i < e.tx.outputs.size(); ++i) {
        UtxoEntry ue;
        ue.amount_atoms = e.tx.outputs[i].amount_atoms;
        ue.script_pubkey = e.tx.outputs[i].script_pubkey;
        ue.coinbase = false;
        ue.height = next_height;
        view.add(OutPoint{tid, i}, ue);
      }
      fees += v.fee_atoms;
      block_txs.push_back(e.tx);
      included.push_back(e.id);
    }

    const std::uint64_t subsidy = phantomx::rewards::block_subsidy_atoms(next_height);
    const std::uint64_t verifier_share = phantomx::rewards::bps_share(subsidy, phantomx::rewards::kVerifierShareBps);
    std::uint64_t contributor_share = subsidy - verifier_share;
    if (g_poi_running.load()) {
      // Deterministic bounded settlement: contributor share scaled by PoI score [0,100].
      // Remainder stays unminted in this block output (conservative; no inflation).
      const double factor = std::clamp(poi_settlement_score / 100.0, 0.0, 1.0);
      contributor_share = static_cast<std::uint64_t>(static_cast<double>(contributor_share) * factor);
    }
    // All non-verifier subsidy (PoI contributors, 90%) goes to the mining
    // reward address. There is no pool reward category in the master plan.
    const std::uint64_t miner_share = contributor_share;
    json_t* vpick = pick_rolling_verifier(static_cast<std::uint64_t>(next_height));
    const char* vaddr = vpick ? json_string_value(json_object_get(vpick, "address")) : nullptr;
    bool paid_verifier = false;
    if (vaddr && verifier_share > 0) {
      auto vscript = phantomx::script_pubkey_for_address(vaddr, phantomx::address_hrp(phantomx::Network::kRegtest));
      if (vscript.ok()) {
        block_txs[0].outputs[0].amount_atoms = miner_share + fees;
        TxOut vout;
        vout.amount_atoms = verifier_share;
        vout.script_pubkey = vscript.value();
        block_txs[0].outputs.push_back(vout);
        paid_verifier = true;
      }
    }
    if (!paid_verifier) {
      block_txs[0].outputs[0].amount_atoms = subsidy + fees;
    }
    if (vpick) json_decref(vpick);

    // Build header and mine
    std::uint64_t ts = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    if (next_height > 0) {
      std::vector<std::uint64_t> times;
      std::uint32_t hh = next_height - 1;
      for (int i = 0; i < 11; ++i) {
        auto hhr = chain.get_hash_by_height(hh);
        if (!hhr.ok() || !hhr.value().has_value()) break;
        auto bi = chain.get_block_index(*hhr.value());
        if (!bi.ok() || !bi.value().has_value()) break;
        times.push_back(bi.value()->timestamp);
        if (hh == 0) break;
        --hh;
      }
      if (!times.empty()) {
        std::sort(times.begin(), times.end());
        const auto mtp = times[times.size() / 2];
        if (ts <= mtp) ts = mtp + 1;
      }
    }
    Block block;
    block.header.version = kBlockVersionV1;
    block.header.prev_hash = prev;
    block.header.timestamp = ts;
    block.header.height = next_height;
    block.header.bits = phantomx::consensus::kRegtestPowBits;
    block.header.nonce = 0;
    block.transactions = block_txs;

    auto recompute_merkle = [&block]() {
      std::vector<Hash256Bytes> ids;
      ids.reserve(block.transactions.size());
      for (const auto& tx : block.transactions) ids.push_back(txid(tx));
      block.header.merkle_root = merkle_root(std::move(ids));
    };
    recompute_merkle();

    for (std::uint32_t nonce = 0;; ++nonce) {
      block.header.nonce = nonce;
      const auto h = block_hash(block.header);
      if (phantomx::consensus::check_proof_of_work(h, block.header.bits)) break;
      if (nonce == 0xFFFFFFFFu) return MakeError(ErrorCode::kInternalError, "nonce exhausted");
    }

    auto applied = chain.apply_block(block);
    if (!applied.ok()) return applied.error();
    for (const auto& id : included) mempool.remove(id);
    // Purge mempool entries that are no longer valid.
    auto utxos_after = chain.load_utxo_set();
    if (utxos_after.ok()) {
      std::vector<Hash256Bytes> stale;
      for (const auto& [_, e] : mempool.entries()) {
        auto v = validate_transaction(e.tx, utxos_after.value(), false, block.header.height + 1);
        if (!v.ok) stale.push_back(e.id);
      }
      for (const auto& id : stale) mempool.remove(id);
    }
    return block_hash(block.header);
  };

  server.register_method("generatetoaddress", [&chain, mine_one](const RpcRequest& req) {
    auto ait = req.params.find("address");
    if (ait == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing param address");
    std::uint32_t n = 1;
    if (auto nit = req.params.find("nblocks"); nit != req.params.end()) {
      try {
        const auto nv = std::stoull(nit->second);
        if (nv == 0 || nv > 1000) return err(req.id, RpcErrorCode::kInvalidParams, "invalid nblocks");
        n = static_cast<std::uint32_t>(nv);
      } catch (...) { return err(req.id, RpcErrorCode::kInvalidParams, "invalid nblocks"); }
    }
    auto tip = chain.tip();
    const std::uint32_t cur_h = (tip.ok() && tip.value().has_value()) ? tip.value()->height : 0u;
    if (cur_h + n > 100u) {
      return err(req.id, RpcErrorCode::kInvalidParams, "generatetoaddress is allowed only up to height 100");
    }
    json_t* arr = json_array();
    for (std::uint32_t i = 0; i < n; ++i) {
      auto r = mine_one(ait->second);
      if (!r.ok()) {
        json_decref(arr);
        return err(req.id, RpcErrorCode::kInternalError, r.error().to_string());
      }
      json_array_append_new(arr, json_string(to_hex_local(r.value()).c_str()));
    }
    char* text = json_dumps(arr, JSON_COMPACT);
    std::string out = text != nullptr ? text : "[]";
    if (text != nullptr) free(text);
    json_decref(arr);
    return ok(req.id, {{"hashes_json", out}, {"count", std::to_string(n)}});
  });

  server.register_method("generateblock", [&chain, mine_one](const RpcRequest& req) {
    auto ait = req.params.find("address");
    if (ait == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing param address");
    auto tip = chain.tip();
    const std::uint32_t cur_h = (tip.ok() && tip.value().has_value()) ? tip.value()->height : 0u;
    if (cur_h >= 100u) {
      return err(req.id, RpcErrorCode::kInvalidParams, "generatetoaddress is allowed only up to height 100");
    }
    auto r = mine_one(ait->second);
    if (!r.ok()) return err(req.id, RpcErrorCode::kInternalError, r.error().to_string());
    return ok(req.id, {{"hash", to_hex_local(r.value())}});
  });

  // --- PoI / Mining pool integration ---
  server.register_method("getmininginfo", [&chain](const RpcRequest& req) {
    auto tip = chain.tip();
    const auto h = tip.ok() && tip.value().has_value() ? tip.value()->height : 0u;
    const auto now_sec = static_cast<std::uint64_t>(std::time(nullptr));
    const auto active_miners = poi_active_miners(now_sec);
    double network_mbps = 0.0;
    {
      std::scoped_lock lk(g_poi_mu);
      for (const auto& [_, p] : g_poi_participants) {
        if (!p.running || p.last_heartbeat == 0) continue;
        if (now_sec < p.last_heartbeat || (now_sec - p.last_heartbeat) > kPoiHeartbeatWindowSec) continue;
        const double pm = poi_bandwidth_mbps(p.bandwidth_bytes, p.bandwidth_unit);
        if (std::isfinite(pm) && pm > 0.0) network_mbps += pm;
      }
    }
    if (network_mbps <= 0.0 && g_poi_running.load()) {
      const double local = poi_bandwidth_mbps(g_poi_bandwidth_bytes.load(), g_poi_bandwidth_unit);
      if (std::isfinite(local) && local > 0.0) network_mbps = local;
    }
    return ok(req.id, {{"algorithm", "poi"},
                       {"pow", "false"},
                       {"poi", "true"},
                       {"height", std::to_string(h)},
                       {"chain", "regtest"},
                       {"network_capacity_metric", "poi_bandwidth"},
                       {"poi_network_bandwidth_mbps", std::to_string(static_cast<std::uint64_t>(std::llround(network_mbps)))},
                       {"poi_network_bandwidth_display", phantomx::poi::format_poi_bandwidth_si(network_mbps)},
                       {"poi_bandwidth_unit", "Mbps"},
                       {"active_miners", std::to_string(active_miners)}});
  });

  server.register_method("getblocktemplate", [&chain, pick_rolling_verifier](const RpcRequest& req) {
    auto tip = chain.tip();
    const bool has_tip = tip.ok() && tip.value().has_value();
    const std::uint32_t next_h = has_tip ? tip.value()->height + 1 : 0;
    const Hash256Bytes prev = has_tip ? tip.value()->hash : Hash256Bytes{};
    const std::uint64_t subsidy = phantomx::rewards::block_subsidy_atoms(next_h);
    const std::uint64_t verifier_share = phantomx::rewards::bps_share(subsidy, phantomx::rewards::kVerifierShareBps);
    const std::uint64_t contributor_share = subsidy - verifier_share;
    const std::uint64_t miner_share = contributor_share;
    json_t* obj = json_object();
    json_object_set_new(obj, "algorithm", json_string("poi"));
    json_object_set_new(obj, "height", json_integer(static_cast<json_int_t>(next_h)));
    json_object_set_new(obj, "previousblockhash", json_string(to_hex_local(prev).c_str()));
    json_object_set_new(obj, "bits", json_string("207fffff"));
    json_object_set_new(obj, "coinbasevalue", json_integer(static_cast<json_int_t>(subsidy)));
    json_object_set_new(obj, "poi_required", json_true());
    json_t* split = json_object();
    json_object_set_new(split, "contributor_percent", json_integer(90));
    json_object_set_new(split, "verifier_percent", json_integer(10));
    json_object_set_new(split, "miner_subsidy_atoms", json_integer(static_cast<json_int_t>(miner_share)));
    json_object_set_new(split, "verifier_subsidy_atoms", json_integer(static_cast<json_int_t>(verifier_share)));
    json_t* vpick = pick_rolling_verifier(static_cast<std::uint64_t>(next_h));
    if (vpick) {
      const char* vaddr = json_string_value(json_object_get(vpick, "address"));
      const char* vid = json_string_value(json_object_get(vpick, "id"));
      if (vaddr) json_object_set_new(split, "verifier_address", json_string(vaddr));
      if (vid) json_object_set_new(split, "verifier_id", json_string(vid));
      json_decref(vpick);
    } else {
      json_object_set_new(split, "verifier_address", json_null());
      json_object_set_new(split, "note", json_string("no verifier registered; full subsidy to miner"));
      json_object_set_new(split, "miner_subsidy_atoms", json_integer(static_cast<json_int_t>(subsidy)));
      json_object_set_new(split, "verifier_subsidy_atoms", json_integer(0));
    }
    json_object_set_new(obj, "coinbase_split", split);
    char* text = json_dumps(obj, JSON_COMPACT);
    std::string out = text != nullptr ? text : "{}";
    if (text != nullptr) free(text);
    json_decref(obj);
    return ok(req.id, {{"template_json", out}, {"algorithm", "poi"}});
  });

  server.register_method("submitblock", [&chain, &mempool](const RpcRequest& req) {
    const auto it = req.params.find("hex");
    if (it == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing param hex");
    std::vector<std::uint8_t> raw;
    if (!from_hex(it->second, raw)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid hex");
    auto blk = phantomx::deserialize_block(raw);
    if (!blk.ok()) return err(req.id, RpcErrorCode::kInvalidParams, blk.error().to_string());
    auto applied = chain.apply_block(blk.value());
    if (!applied.ok()) return err(req.id, RpcErrorCode::kInvalidParams, applied.error().to_string());
    for (const auto& tx : blk.value().transactions) mempool.remove(phantomx::txid(tx));
    return ok(req.id, {{"status", "accepted"}, {"hash", to_hex_local(phantomx::block_hash(blk.value().header))}});
  });

  auto parse_id32 = [](const std::string& hex, std::array<std::uint8_t,32>& out) -> bool {
    std::vector<std::uint8_t> b;
    if (!from_hex(hex, b) || b.size() != 32) return false;
    std::copy(b.begin(), b.end(), out.begin());
    return true;
  };

  server.register_method("pool_register_worker", [&pool, parse_id32](const RpcRequest& req) {
    const auto wit = req.params.find("worker_id");
    const auto oit = req.params.find("operator_id");
    const auto rit = req.params.find("role");
    const auto ait = req.params.find("auth_hash");
    if (wit == req.params.end() || oit == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing worker_id/operator_id");
    phantomx::pool::WorkerRecord w;
    if (!parse_id32(wit->second, w.worker_id) || !parse_id32(oit->second, w.operator_id))
      return err(req.id, RpcErrorCode::kInvalidParams, "invalid id hex");
    const std::string role = rit == req.params.end() ? "poi" : rit->second;
    if (role == "verifier") w.role = phantomx::pool::PoolRole::kVerifierWorker;
    else if (role == "operator") w.role = phantomx::pool::PoolRole::kOperator;
    else w.role = phantomx::pool::PoolRole::kPoiWorker;
    w.auth_token_hash = ait == req.params.end() ? std::string("none") : ait->second;
    auto r = pool.register_worker(w, static_cast<std::uint64_t>(std::time(nullptr)));
    if (!r.ok()) return err(req.id, RpcErrorCode::kInvalidParams, r.error().to_string());
    return ok(req.id, {{"status", "registered"}, {"worker_id", wit->second}});
  });

  server.register_method("pool_submit_share", [&pool, parse_id32](const RpcRequest& req) {
    const auto wit = req.params.find("worker_id");
    const auto eit = req.params.find("epoch");
    const auto uit = req.params.find("units");
    const auto hit = req.params.find("evidence_hash");
    const auto rit = req.params.find("role");
    if (wit == req.params.end() || eit == req.params.end() || uit == req.params.end() || hit == req.params.end())
      return err(req.id, RpcErrorCode::kInvalidParams, "missing worker_id/epoch/units/evidence_hash");
    phantomx::pool::ShareRecord s;
    if (!parse_id32(wit->second, s.worker_id)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid worker_id");
    try { s.epoch = std::stoull(eit->second); } catch (...) { return err(req.id, RpcErrorCode::kInvalidParams, "invalid epoch"); }
    try { s.units = std::stoull(uit->second); } catch (...) { return err(req.id, RpcErrorCode::kInvalidParams, "invalid units"); }
    s.evidence_hash_hex = hit->second;
    const std::string role = rit == req.params.end() ? "poi" : rit->second;
    s.role = (role == "verifier") ? phantomx::pool::PoolRole::kVerifierWorker : phantomx::pool::PoolRole::kPoiWorker;
    auto r = pool.submit_share(s);
    if (!r.ok()) return err(req.id, RpcErrorCode::kInvalidParams, r.error().to_string());
    return ok(req.id, {{"status", "accepted"}, {"worker_id", wit->second}});
  });

  server.register_method("pool_getdashboard", [&pool](const RpcRequest& req) {
    auto d = pool.pool_dashboard();
    return ok(req.id, {{"workers", std::to_string(d.workers)},
                       {"active_workers", std::to_string(d.active_workers)},
                       {"total_shares", std::to_string(d.total_shares)},
                       {"total_paid_atoms", std::to_string(d.total_paid_atoms)}});
  });

  server.register_method("pool_getworker", [&pool, parse_id32](const RpcRequest& req) {
    const auto wit = req.params.find("worker_id");
    const auto eit = req.params.find("epoch");
    if (wit == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing worker_id");
    std::array<std::uint8_t,32> id{};
    if (!parse_id32(wit->second, id)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid worker_id");
    std::uint64_t epoch = 0;
    if (eit != req.params.end()) { try { epoch = std::stoull(eit->second); } catch (...) { return err(req.id, RpcErrorCode::kInvalidParams, "invalid epoch"); } }
    auto wd = pool.worker_dashboard(id, epoch);
    wd.estimated_reward_atoms = pool.estimate_reward_atoms(id, phantomx::pool::PoolRole::kPoiWorker, epoch, phantomx::rewards::block_subsidy_atoms(1));
    return ok(req.id, {{"worker_id", wit->second},
                       {"epoch", std::to_string(epoch)},
                       {"share_units", std::to_string(wd.share_units)},
                       {"estimated_reward_atoms", std::to_string(wd.estimated_reward_atoms)},
                       {"paid_atoms", std::to_string(wd.paid_atoms)}});
  });

  server.register_method("pool_record_payout", [&pool, parse_id32](const RpcRequest& req) {
    // Daemon support: record payout only when epoch is finalized by PoI+Verifier attestation.
    // PTM-POOL-AUTH: payout recording is operator/admin action, not ordinary RPC.
    const char* admin_env = std::getenv("PHANTOMX_ADMIN_TOKEN");
    auto at = req.params.find("admin_token");
    if (!admin_env || !*admin_env || at == req.params.end() || at->second != admin_env) {
      return err(req.id, RpcErrorCode::kForbidden, "pool_record_payout requires admin_token");
    }
    // Fee/minimum payout/send-multiple are handled by miningpoi/web, not daemon.
    const auto wit = req.params.find("worker_id");
    const auto eit = req.params.find("epoch");
    const auto amt = req.params.find("amount_atoms");
    const auto cat = req.params.find("category");
    if (wit == req.params.end() || eit == req.params.end() || amt == req.params.end())
      return err(req.id, RpcErrorCode::kInvalidParams, "missing worker_id/epoch/amount_atoms");
    phantomx::pool::PayoutRecord p;
    if (!parse_id32(wit->second, p.worker_id)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid worker_id");
    try { p.epoch = std::stoull(eit->second); } catch (...) { return err(req.id, RpcErrorCode::kInvalidParams, "invalid epoch"); }
    try { p.amount_atoms = std::stoull(amt->second); } catch (...) { return err(req.id, RpcErrorCode::kInvalidParams, "invalid amount_atoms"); }
    p.category = cat == req.params.end() ? "poi" : cat->second;
    const auto finalized_epoch = g_poi_finalized_height.load();
    auto r = pool.record_payout(p, finalized_epoch);
    if (!r.ok()) return err(req.id, RpcErrorCode::kInvalidParams, r.error().to_string());
    return ok(req.id, {{"status","recorded"},{"worker_id",wit->second},{"epoch",std::to_string(p.epoch)},{"amount_atoms",std::to_string(p.amount_atoms)},{"finalized_epoch",std::to_string(finalized_epoch)}});
  });

  server.register_method("submitpoievidence", [&pool, parse_id32](const RpcRequest& req) {
    // Convenience alias: accepts worker_id, epoch, evidence_hash, bandwidth, uptime, latency, relay (milli 0..100000)
    const auto wit = req.params.find("worker_id");
    const auto eit = req.params.find("epoch");
    const auto hit = req.params.find("evidence_hash");
    if (wit == req.params.end() || eit == req.params.end() || hit == req.params.end())
      return err(req.id, RpcErrorCode::kInvalidParams, "missing worker_id/epoch/evidence_hash");
    phantomx::poi::PoiMetrics m;
    auto getm = [&](const char* k)->phantomx::poi::ScoreMilli { auto it=req.params.find(k); if (it==req.params.end()) return 0u; try { return static_cast<phantomx::poi::ScoreMilli>(std::stoul(it->second)); } catch (...) { return 0u; } };
    m.bandwidth = phantomx::poi::clamp_score(getm("bandwidth"));
    m.uptime    = phantomx::poi::clamp_score(getm("uptime"));
    m.latency   = phantomx::poi::clamp_score(getm("latency"));
    m.relay     = phantomx::poi::clamp_score(getm("relay"));
    auto score = phantomx::poi::compute_poi_score(m);
    phantomx::pool::ShareRecord s;
    if (!parse_id32(wit->second, s.worker_id)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid worker_id");
    try { s.epoch = std::stoull(eit->second); } catch (...) { return err(req.id, RpcErrorCode::kInvalidParams, "invalid epoch"); }
    s.units = score.total == 0 ? 1 : score.total; // use PoI score as work units
    s.evidence_hash_hex = hit->second;
    s.role = phantomx::pool::PoolRole::kPoiWorker;
    auto r = pool.submit_share(s);
    if (!r.ok()) return err(req.id, RpcErrorCode::kInvalidParams, r.error().to_string());
    return ok(req.id, {{"status", "accepted"}, {"poi_score", std::to_string(score.total)},
                       {"units", std::to_string(s.units)}, {"algorithm", "poi"}});
  });

  // --- Extended chain info (includes tokenomics fields). Replaces minimal handler above. ---
  server.register_method("getblockchaininfo", [&chain, p2p, network](const RpcRequest& req) {
    auto tip = chain.tip();
    std::unordered_map<std::string, std::string> r;
    const std::uint32_t blocks = tip.ok() && tip.value().has_value() ? tip.value()->height : 0;
    const std::uint32_t headers = blocks;
    const double progress = headers == 0 ? 1.0 : static_cast<double>(blocks) / static_cast<double>(headers);
    const auto net_name = std::string(network_name(network));
    r["chain"] = net_name;
    r["network"] = net_name;
    r["chain_height"] = std::to_string(blocks);
    r["blocks"] = std::to_string(blocks);
    r["headers"] = std::to_string(headers);
    r["verificationprogress"] = std::to_string(progress > 1.0 ? 1.0 : progress);
    r["initialblockdownload"] = blocks < headers ? "true" : "false";
    r["bestblockhash"] = tip.ok() && tip.value().has_value() ? to_hex_local(tip.value()->hash) : "";
    r["connections"] = p2p ? std::to_string(p2p->peer_count()) : "0";
    r["max_block_size"] = "1048576";
    r["confirmations"] = "0";
    r["initial_block_reward_atoms"] = "2500000000";
    r["halving_interval"] = "2100000";
    r["max_supply_atoms"] = "10500000000000000";
    return ok(req.id, std::move(r));
  });

  server.register_method("getchaintips", [&chain](const RpcRequest& req) {
    auto tip = chain.tip();
    if (!tip.ok() || !tip.value().has_value()) return ok(req.id, {{"tips", "[]"}});
    return ok(req.id, {{"tips", "[{\"hash\":\"" + to_hex_local(tip.value()->hash) + "\",\"height\":\"" + std::to_string(tip.value()->height) + "\",\"status\":\"active\"}]"}}); 
  });

  server.register_method("gettxout", [&chain, parse_id32](const RpcRequest& req) {
    auto txid_it = req.params.find("txid"); auto vout_it = req.params.find("vout");
    if (txid_it == req.params.end() || vout_it == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing txid/vout");
    Hash256Bytes txh{}; if (!parse_id32(txid_it->second, txh)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid txid");
    std::uint32_t vout = static_cast<std::uint32_t>(std::stoul(vout_it->second));
    auto u = chain.get_utxo(OutPoint{txh, vout});
    if (!u.ok()) return err(req.id, RpcErrorCode::kInternalError, u.error().to_string());
    if (!u.value().has_value()) return ok(req.id, {{"found", "false"}});
    const auto& e = *u.value();
    return ok(req.id, {{"found","true"},{"amount_atoms",std::to_string(e.amount_atoms)},{"coinbase",e.coinbase?"true":"false"},{"height",std::to_string(e.height)}});
  });

  server.register_method("getrawtransaction", [&mempool, parse_id32](const RpcRequest& req) {
    auto h = req.params.find("txid"); if (h == req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing txid");
    Hash256Bytes txh{}; if (!parse_id32(h->second, txh)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid txid");
    if (mempool.contains(txh)) {
      auto e = mempool.get(txh);
      return ok(req.id, {{"txid", h->second}, {"in_mempool","true"}, {"confirmations","0"}, {"fee_atoms", e.has_value()?std::to_string(e->fee_atoms):"0"}});
    }
    // chain lookup limited: report honest status
    return ok(req.id, {{"txid", h->second}, {"found", "false"}, {"status", "foundation: chain tx index lookup limited; use block scan"}});
  });

  server.register_method("addnode", [p2p](const RpcRequest& req) {
    if (!p2p) return err(req.id, RpcErrorCode::kInternalError, "p2p disabled");
    auto it=req.params.find("node"); if (it==req.params.end()) it=req.params.find("addr");
    if (it==req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing node=host:port");
    auto pos = it->second.rfind(':');
    if (pos==std::string::npos) return err(req.id, RpcErrorCode::kInvalidParams, "expected host:port");
    std::string host = it->second.substr(0,pos);
    std::uint16_t port = static_cast<std::uint16_t>(std::stoul(it->second.substr(pos+1)));
    auto r = p2p->connect(host, port);
    if (!r.ok()) return err(req.id, RpcErrorCode::kInternalError, r.error().to_string());
    return ok(req.id, {{"status","connecting"},{"node",it->second}});
  });
  server.register_method("disconnectnode", [](const RpcRequest& req) {
    return err(req.id, RpcErrorCode::kInternalError, "not implemented: disconnect by id not yet exposed");
  });

  // PoI
  server.register_method("getpoiinfo", [&pool, p2p, &chain, load_verifiers, verifier_online](const RpcRequest& req) {
    (void)chain;
    const auto now_sec = static_cast<std::uint64_t>(std::time(nullptr));
    const auto active_miners = poi_active_miners(now_sec);
    const auto epoch = poi_epoch(active_miners);

    // Compute average score across active participants; fallback to local config.
    double avg_score = 0.0;
    std::uint64_t scored = 0;
    bool any_partial = false;
    bool any_available = false;
    std::string bw_comp = "Not Available";
    std::string lat_comp = "Not Available";
    std::string up_comp = "Not Available";
    std::string rel_comp = "Not Available";
    std::string net_comp = "Not Available";
    const double measured_latency = poi_latency_ms(p2p);
    const double measured_relay = poi_relay_ratio(p2p);
    {
      std::scoped_lock lk(g_poi_mu);
      for (const auto& [_, p] : g_poi_participants) {
        if (!p.running) continue;
        if (p.last_heartbeat == 0) continue;
        if (now_sec < p.last_heartbeat || (now_sec - p.last_heartbeat) > kPoiHeartbeatWindowSec) continue;
        const double lms = p.latency_ms >= 0.0 ? p.latency_ms : measured_latency;
        const double rr = p.relay_ratio >= 0.0 ? p.relay_ratio : measured_relay;
        const double up = poi_uptime_ratio(p, now_sec);
        auto sr = poi_compute_score(p.bandwidth_bytes, p.bandwidth_unit, lms, rr, up, active_miners);
        if (sr.available) {
          avg_score += sr.score;
          ++scored;
          any_available = true;
          any_partial = any_partial || sr.partial;
          bw_comp = sr.bandwidth_component;
          lat_comp = sr.latency_component;
          up_comp = sr.uptime_component;
          rel_comp = sr.relay_component;
          net_comp = sr.network_component;
        }
      }
    }
    if (scored == 0) {
      // No active heartbeat participants; if node is running, compute from local config with missing uptime.
      if (g_poi_running.load()) {
        const double lms = poi_latency_ms(p2p);
        const double rratio = poi_relay_ratio(p2p);
        auto sr = poi_compute_score(g_poi_bandwidth_bytes.load(), g_poi_bandwidth_unit, lms, rratio, -1.0, active_miners);
        if (sr.available) {
          avg_score = sr.score;
          scored = 1;
          any_available = true;
          any_partial = sr.partial;
          bw_comp = sr.bandwidth_component;
          lat_comp = sr.latency_component;
          up_comp = sr.uptime_component;
          rel_comp = sr.relay_component;
          net_comp = sr.network_component;
        }
      }
    } else {
      avg_score /= static_cast<double>(scored);
    }

    const auto base_ms = 60000ULL; // base blocktime; Phase A default
    const auto target_ms = poi_target_block_interval_ms(base_ms, any_available ? avg_score : 0.0, epoch);

    std::unordered_map<std::string, std::string> r;
    r["algorithm"] = "poi";
    r["status"] = g_poi_running.load() ? "running" : "stopped";
    r["running"] = g_poi_running.load() ? "true" : "false";
    r["workers"] = std::to_string(pool.pool_dashboard().workers);
    r["connections"] = p2p ? std::to_string(p2p->peer_count()) : "0";
    const double local_mbps = poi_bandwidth_mbps(g_poi_bandwidth_bytes.load(), g_poi_bandwidth_unit);
    double network_mbps = 0.0;
    {
      std::scoped_lock lk(g_poi_mu);
      for (const auto& [_, p] : g_poi_participants) {
        if (!p.running || p.last_heartbeat == 0) continue;
        if (now_sec < p.last_heartbeat || (now_sec - p.last_heartbeat) > kPoiHeartbeatWindowSec) continue;
        const double pm = poi_bandwidth_mbps(p.bandwidth_bytes, p.bandwidth_unit);
        if (std::isfinite(pm) && pm > 0.0) network_mbps += pm;
      }
    }
    if (network_mbps <= 0.0 && g_poi_running.load() && std::isfinite(local_mbps) && local_mbps > 0.0) network_mbps = local_mbps;
    r["bandwidth_bytes"] = std::to_string(g_poi_bandwidth_bytes.load());
    r["bandwidth_mbps"] = std::to_string(static_cast<std::uint64_t>(std::llround(std::max(0.0, local_mbps))));
    r["poi_network_bandwidth_mbps"] = std::to_string(static_cast<std::uint64_t>(std::llround(network_mbps)));
    r["poi_network_bandwidth_display"] = phantomx::poi::format_poi_bandwidth_si(network_mbps);
    r["poi_bandwidth_unit"] = "Mbps";
    r["network_capacity_metric"] = "poi_bandwidth";
    r["epoch"] = std::to_string(epoch);
    r["active_miners"] = std::to_string(active_miners);
    r["score"] = any_available ? std::to_string(static_cast<int>(std::lround(avg_score))) : "Not Available";
    r["score_partial"] = any_partial ? "true" : "false";
    r["score_bandwidth"] = bw_comp;
    r["score_latency"] = lat_comp;
    r["score_uptime"] = up_comp;
    r["score_relay"] = rel_comp;
    r["score_network"] = net_comp;
    r["latency_ms"] = measured_latency >= 0.0 ? std::to_string(static_cast<int>(std::lround(measured_latency))) : "Not Available";
    r["relay_ratio"] = measured_relay >= 0.0 ? std::to_string(measured_relay) : "Not Available";
    r["uptime_window_sec"] = std::to_string(kPoiUptimeWindowSec);
    r["score_weights"] = "bandwidth=0.30,latency=0.20,uptime=0.25,relay=0.15,network=0.10";
    r["score_formula_version"] = "v2";
    r["blocktime_base_ms"] = std::to_string(base_ms);
    r["target_block_interval_ms"] = std::to_string(target_ms);
    std::uint64_t ver_total = 0, ver_online = 0;
    {
      json_t* varr = load_verifiers();
      ver_total = static_cast<std::uint64_t>(json_array_size(varr));
      size_t vi; json_t* vv;
      json_array_foreach(varr, vi, vv) { if (verifier_online(vv)) ++ver_online; }
      json_decref(varr);
    }
    g_poi_verifiers_online.store(ver_online);
    r["autoproduce"] = g_poi_autoproduce.load() ? "true" : "false";
    r["producer_mode"] = "poi";
    r["consensus_mode"] = "poi";
    r["finality_model"] = "poi_attested";
    r["finality_quorum"] = std::to_string(g_poi_finality_quorum.load());
    r["q_min"] = std::to_string(g_poi_q_min.load());
    {
      const auto fh = g_poi_finalized_height.load();
      const auto tip_h = [&](){ auto t=chain.tip(); return t.ok()&&t.value().has_value()?t.value()->height:0; }();
      r["finality_status"] = (fh >= tip_h && tip_h > 0) ? "finalized" : (fh == 0 && tip_h == 0 ? "genesis" : "pending_attestation");
    }
    r["blocks_found"] = std::to_string(g_poi_blocks_found.load());
    r["last_block_at"] = std::to_string(g_poi_last_block_at.load());
    { std::scoped_lock lk(g_poi_mu); r["last_block_hash"] = g_poi_last_block_hash; }
    r["verifiers_total"] = std::to_string(ver_total);
    r["verifiers_online"] = std::to_string(ver_online);
    r["epoch_formula"] = "max(1,ceil(active_miners/K)); K=10, window=600s";
    r["score_formula"] = "100*(0.30*bw+0.20*lat+0.25*uptime+0.15*relay+0.10*network); bw=B/(B+500Mbps), lat=1/(1+L/100ms), uptime=verified_online/window, missing components reweighted and never treated as perfect";
    return ok(req.id, std::move(r));
  });
  server.register_method("startpoi", [p2p, save_poi_participants](const RpcRequest& req) {
    g_poi_running.store(true);
    if (auto it = req.params.find("bandwidth_bytes"); it != req.params.end()) {
      try { g_poi_bandwidth_bytes.store(std::stoull(it->second)); } catch (...) {}
    }
    if (auto it = req.params.find("bandwidth_unit"); it != req.params.end()) g_poi_bandwidth_unit = it->second;

    std::string participant = "local";
    if (auto it = req.params.find("participant"); it != req.params.end() && !it->second.empty()) participant = it->second;
    else if (auto it2 = req.params.find("address"); it2 != req.params.end() && !it2->second.empty()) participant = it2->second;
    const std::string owner = poi_effective_owner(participant, req.params);
    const std::string machine = poi_effective_machine(participant, req.params);
    const auto now_sec_pre = static_cast<std::uint64_t>(std::time(nullptr));
    {
      std::scoped_lock lk(g_poi_mu);
      auto idv = poi_verify_identity_claim(req.params, owner, machine, participant, now_sec_pre);
      if (!idv.ok()) return err(req.id, idv.error().code() == ErrorCode::kUnauthorized ? RpcErrorCode::kUnauthorized : RpcErrorCode::kForbidden, idv.error().to_string());
    }
    if (auto it = req.params.find("reward_address"); it != req.params.end() && !it->second.empty()) {
      std::scoped_lock lk(g_poi_mu);
      g_poi_reward_address = it->second;
    } else if (!owner.empty()) {
      std::scoped_lock lk(g_poi_mu);
      if (g_poi_reward_address.empty()) g_poi_reward_address = owner;
    }
    {
      std::scoped_lock lk(g_poi_mu);
      std::string conflict;
      if (poi_ip_reject_conflict(g_rpc_remote_ip, owner, participant, conflict)) {
        return err(req.id, RpcErrorCode::kForbidden, conflict);
      }
      poi_upsert_participant_locked(participant, g_poi_bandwidth_bytes.load(), g_poi_bandwidth_unit, true);
      auto& p = g_poi_participants[participant];
      p.owner_address = owner;
      p.machine_id = machine;
      p.source_ip = g_rpc_remote_ip;
      poi_ip_bind(g_rpc_remote_ip, owner, participant, now_sec_pre);
    }
    save_poi_participants();

    const auto now_sec = static_cast<std::uint64_t>(std::time(nullptr));
    const auto active_miners = poi_active_miners(now_sec);
    const auto epoch = poi_epoch(active_miners);
    const double lms = poi_latency_ms(p2p);
    const double rratio = poi_relay_ratio(p2p);
    double up = -1.0;
    {
      std::scoped_lock lk(g_poi_mu);
      auto itp = g_poi_participants.find(participant);
      if (itp != g_poi_participants.end()) up = poi_uptime_ratio(itp->second, now_sec);
    }
    auto sr = poi_compute_score(g_poi_bandwidth_bytes.load(), g_poi_bandwidth_unit, lms, rratio, up, active_miners);
    const double aggregate = poi_owner_aggregate_score(owner, now_sec, sr.score);
    const auto target_ms = poi_target_block_interval_ms(60000ULL, sr.available ? aggregate : 0.0, epoch);

    return ok(req.id, {{"status","running"},{"running","true"},
                       {"bandwidth_bytes", std::to_string(g_poi_bandwidth_bytes.load())},
                       {"bandwidth_mbps", std::to_string(static_cast<std::uint64_t>(std::llround(poi_bandwidth_mbps(g_poi_bandwidth_bytes.load(), g_poi_bandwidth_unit))))},
                       {"participant", participant},
                       {"owner_address", owner},
                       {"machine_id", machine},
                       {"source_ip", g_rpc_remote_ip},
                       {"active_miners", std::to_string(active_miners)},
                       {"epoch", std::to_string(epoch)},
                       {"score", sr.available ? std::to_string(static_cast<int>(std::lround(sr.score))) : "Not Available"},
                       {"aggregate_score", sr.available ? std::to_string(static_cast<int>(std::lround(aggregate))) : "Not Available"},
                       {"score_partial", sr.partial ? "true" : "false"},
                       {"score_bandwidth", sr.bandwidth_component},
                       {"score_latency", sr.latency_component},
                       {"score_uptime", sr.uptime_component},
                       {"score_relay", sr.relay_component},
                       {"score_network", sr.network_component},
                       {"score_weights", "bandwidth=0.30,latency=0.20,uptime=0.25,relay=0.15,network=0.10"},
                       {"score_formula_version", "v2"},
                       {"latency_ms", lms >= 0 ? std::to_string(static_cast<int>(std::lround(lms))) : "Not Available"},
                       {"relay_ratio", rratio >= 0 ? std::to_string(rratio) : "Not Available"},
                       {"uptime_ratio", up >= 0 ? std::to_string(up) : "Not Available"},
                       {"target_block_interval_ms", std::to_string(target_ms)},
                       {"producer_mode", "poi"},
                       {"note","PoI production active. Uptime is deterministic heartbeat accounting (not cryptographic measurement proof). Missing uptime is never treated as perfect."}});
  });
  server.register_method("stoppoi", [save_poi_participants](const RpcRequest& req) {
    // PTM-005: participant-scoped stop. Global node-wide PoI disable requires explicit admin token.
    std::string participant = "local";
    if (auto it = req.params.find("participant"); it != req.params.end() && !it->second.empty()) participant = it->second;
    else if (auto it2 = req.params.find("address"); it2 != req.params.end() && !it2->second.empty()) participant = it2->second;
    const bool stop_all = (req.params.count("all") && (req.params.at("all")=="1" || req.params.at("all")=="true"));
    if (stop_all) {
      const char* admin_env = std::getenv("PHANTOMX_ADMIN_TOKEN");
      auto at = req.params.find("admin_token");
      if (!admin_env || !*admin_env || at == req.params.end() || at->second != admin_env) {
        return err(req.id, RpcErrorCode::kForbidden, "global stoppoi requires admin_token");
      }
      std::scoped_lock lk(g_poi_mu);
      g_poi_running.store(false);
      for (auto& [_, p] : g_poi_participants) { p.running = false; p.last_heartbeat = static_cast<std::uint64_t>(std::time(nullptr)); }
      save_poi_participants();
      return ok(req.id, {{"status","stopped"},{"scope","all"},{"running","false"}});
    }

    const auto now_sec = static_cast<std::uint64_t>(std::time(nullptr));
    std::string owner = poi_effective_owner(participant, req.params);
    std::string machine = poi_effective_machine(participant, req.params);
    {
      std::scoped_lock lk(g_poi_mu);
      auto itp = g_poi_participants.find(participant);
      if (itp == g_poi_participants.end()) return err(req.id, RpcErrorCode::kNotFound, "participant not found");
      if (owner.empty()) owner = itp->second.owner_address;
      if (machine.empty()) machine = itp->second.machine_id;
      // Remote caller must prove identity/ownership of the participant being stopped.
      auto idv = poi_verify_identity_claim(req.params, owner, machine, participant, now_sec);
      if (!idv.ok()) return err(req.id, idv.error().code() == ErrorCode::kUnauthorized ? RpcErrorCode::kUnauthorized : RpcErrorCode::kForbidden, idv.error().to_string());
      if (!itp->second.owner_address.empty() && !owner.empty() && itp->second.owner_address != owner) {
        return err(req.id, RpcErrorCode::kForbidden, "owner mismatch for participant");
      }
      itp->second.running = false;
      itp->second.last_heartbeat = now_sec;
      bool any_running = false;
      for (const auto& [_, p] : g_poi_participants) if (p.running) { any_running = true; break; }
      g_poi_running.store(any_running);
    }
    save_poi_participants();
    return ok(req.id, {{"status","stopped"},{"scope","participant"},{"running","false"},{"participant", participant}});
  });
  server.register_method("poi_heartbeat", [p2p, save_poi_participants, count_online_verifiers, poi_required_quorum](const RpcRequest& req) {
    std::string participant = "local";
    if (auto it = req.params.find("participant"); it != req.params.end() && !it->second.empty()) participant = it->second;
    else if (auto it2 = req.params.find("address"); it2 != req.params.end() && !it2->second.empty()) participant = it2->second;
    const std::string owner = poi_effective_owner(participant, req.params);
    const std::string machine = poi_effective_machine(participant, req.params);
    const auto now_sec = static_cast<std::uint64_t>(std::time(nullptr));
    {
      std::scoped_lock lk(g_poi_mu);
      auto itp = g_poi_participants.find(participant);
      if (itp != g_poi_participants.end() && itp->second.last_heartbeat > 0 && now_sec >= itp->second.last_heartbeat &&
          (now_sec - itp->second.last_heartbeat) < kPoiHeartbeatMinIntervalSec) {
        return err(req.id, RpcErrorCode::kRateLimited, "poi heartbeat too frequent");
      }
    }
    {
      std::scoped_lock lk(g_poi_mu);
      auto idv = poi_verify_identity_claim(req.params, owner, machine, participant, now_sec);
      if (!idv.ok()) return err(req.id, idv.error().code() == ErrorCode::kUnauthorized ? RpcErrorCode::kUnauthorized : RpcErrorCode::kForbidden, idv.error().to_string());
      std::string conflict;
      if (poi_ip_reject_conflict(g_rpc_remote_ip, owner, participant, conflict)) {
        return err(req.id, RpcErrorCode::kForbidden, conflict);
      }
    }
    std::uint64_t bw = g_poi_bandwidth_bytes.load();
    if (auto it = req.params.find("bandwidth_bytes"); it != req.params.end()) { try { bw = std::stoull(it->second); } catch (...) {} }
    std::string unit = g_poi_bandwidth_unit;
    if (auto it = req.params.find("bandwidth_unit"); it != req.params.end() && !it->second.empty()) unit = it->second;
    double latency = -1.0;
    if (auto it = req.params.find("latency_ms"); it != req.params.end()) { try { latency = std::stod(it->second); } catch (...) {} }
    double relay = -1.0;
    if (auto it = req.params.find("relay_ratio"); it != req.params.end()) { try { relay = std::stod(it->second); } catch (...) {} }
    const double measured_latency = poi_latency_ms(p2p);
    const double measured_relay = poi_relay_ratio(p2p);
    if (latency < 0.0) latency = measured_latency;
    if (relay < 0.0) relay = measured_relay;
    bool running = g_poi_running.load();
    if (auto it = req.params.find("running"); it != req.params.end()) running = (it->second == "true" || it->second == "1");

    double uptime = -1.0;
    double aggregate = 0.0;
    {
      std::scoped_lock lk(g_poi_mu);
      auto& p = g_poi_participants[participant];
      p.id = participant;
      p.owner_address = owner;
      p.machine_id = machine;
      p.source_ip = g_rpc_remote_ip;
      p.bandwidth_bytes = bw;
      p.bandwidth_unit = unit;
      if (p.uptime_window_start == 0) p.uptime_window_start = now_sec;
      // Deterministic window accounting from heartbeats. Self-reported liveness only;
      // not cryptographic uptime proof.
      p.last_heartbeat = now_sec;
      const std::uint64_t expected = std::max<std::uint64_t>(1, std::min<std::uint64_t>(kPoiUptimeWindowSec, now_sec - p.uptime_window_start) / kPoiHeartbeatIntervalSec + 1);
      if (p.uptime_expected < expected) p.uptime_expected = expected;
      if (p.uptime_ok < p.uptime_expected) ++p.uptime_ok;
      p.running = running;
      if (latency >= 0.0) p.latency_ms = latency;
      if (relay >= 0.0 && std::isfinite(relay)) p.relay_ratio = std::clamp(relay, 0.0, 1.0);
      uptime = poi_uptime_ratio(p, now_sec);
      poi_ip_bind(g_rpc_remote_ip, owner, participant, now_sec);
    }
    g_poi_finality_quorum.store(poi_required_quorum(count_online_verifiers()));
    save_poi_participants();

    const auto active_miners = poi_active_miners(now_sec);
    const auto epoch = poi_epoch(active_miners);
    auto sr = poi_compute_score(bw, unit, latency, relay, uptime, active_miners);
    aggregate = poi_owner_aggregate_score(owner, now_sec, sr.score);
    return ok(req.id, {{"status","ok"},{"participant",participant},{"owner_address",owner},{"machine_id",machine},{"source_ip",g_rpc_remote_ip},{"active_miners",std::to_string(active_miners)},{"epoch",std::to_string(epoch)},{"score", sr.available ? std::to_string(static_cast<int>(std::lround(sr.score))) : "Not Available"},{"aggregate_score", sr.available ? std::to_string(static_cast<int>(std::lround(aggregate))) : "Not Available"},{"score_partial", sr.partial ? "true" : "false"},{"score_bandwidth", sr.bandwidth_component},{"score_latency", sr.latency_component},{"score_uptime", sr.uptime_component},{"score_relay", sr.relay_component},{"score_network", sr.network_component},{"score_weights","bandwidth=0.30,latency=0.20,uptime=0.25,relay=0.15,network=0.10"},{"score_formula_version","v2"}});
  });
  server.register_method("getpoiscore", [p2p](const RpcRequest& req) {
    const std::string participant = req.params.count("participant") ? req.params.at("participant") : "local";
    const auto now_sec = static_cast<std::uint64_t>(std::time(nullptr));
    const auto active_miners = poi_active_miners(now_sec);
    const auto epoch = poi_epoch(active_miners);
    std::uint64_t bw = g_poi_bandwidth_bytes.load();
    std::string unit = g_poi_bandwidth_unit;
    double latency = -1.0;
    double relay = -1.0;
    bool found = false;
    {
      std::scoped_lock lk(g_poi_mu);
      auto itp = g_poi_participants.find(participant);
      if (itp != g_poi_participants.end()) {
        found = true;
        bw = itp->second.bandwidth_bytes;
        unit = itp->second.bandwidth_unit;
        latency = itp->second.latency_ms;
        relay = itp->second.relay_ratio;
      }
    }
    double uptime = -1.0;
    if (!found && participant != "local") {
      return ok(req.id, {{"participant", participant}, {"status", "Not Available"}, {"score", "Not Available"}, {"epoch", std::to_string(epoch)}, {"active_miners", std::to_string(active_miners)}});
    }
    if (found) {
      std::scoped_lock lk(g_poi_mu);
      auto itp = g_poi_participants.find(participant);
      if (itp != g_poi_participants.end()) uptime = poi_uptime_ratio(itp->second, now_sec);
    }
    if (latency < 0.0) latency = poi_latency_ms(p2p);
    if (relay < 0.0) relay = poi_relay_ratio(p2p);
    auto sr = poi_compute_score(bw, unit, latency, relay, uptime, active_miners);
    double aggregate = sr.score;
    std::string owner;
    {
      std::scoped_lock lk(g_poi_mu);
      auto itp = g_poi_participants.find(participant);
      if (itp != g_poi_participants.end()) { owner = itp->second.owner_address; aggregate = poi_owner_aggregate_score(owner, now_sec, sr.score); }
    }
    return ok(req.id, {{"participant", participant}, {"owner_address", owner}, {"status", sr.available ? "ok" : "Not Available"}, {"score", sr.available ? std::to_string(static_cast<int>(std::lround(sr.score))) : "Not Available"}, {"aggregate_score", sr.available ? std::to_string(static_cast<int>(std::lround(aggregate))) : "Not Available"}, {"score_partial", sr.partial ? "true" : "false"}, {"score_bandwidth", sr.bandwidth_component}, {"score_latency", sr.latency_component}, {"score_uptime", sr.uptime_component}, {"score_relay", sr.relay_component}, {"score_network", sr.network_component}, {"score_weights","bandwidth=0.30,latency=0.20,uptime=0.25,relay=0.15,network=0.10"}, {"score_formula_version","v2"}, {"uptime_ratio", uptime >= 0.0 ? std::to_string(uptime) : "Not Available"}, {"epoch", std::to_string(epoch)}, {"active_miners", std::to_string(active_miners)}});
  });
  server.register_method("getparticipant", [](const RpcRequest& req) {
    auto it=req.params.find("id"); return ok(req.id, {{"id", it==req.params.end()?"":it->second}, {"status","foundation"}});
  });

  // Verifier
  server.register_method("getverifierinfo", [](const RpcRequest& req) { return ok(req.id, {{"status","foundation"}}); });
  server.register_method("registerverifier", [&server](const RpcRequest& req) { return server.call(RpcRequest{req.id, "verifier_register", req.params, req.auth_token}, static_cast<std::uint64_t>(std::time(nullptr))); });
  server.register_method("getverification", [](const RpcRequest& req) { return ok(req.id, {{"status","foundation"},{"id", req.params.count("id")?req.params.at("id"):""}}); });
  server.register_method("getverifierassignment", [](const RpcRequest& req) { return ok(req.id, {{"status","foundation"}}); });

  // Aggregator (Prompt 08 wiring)
  server.register_method("aggregator_register", [&aggregator, parse_id32](const RpcRequest& req) {
    auto pk=req.params.find("aggregator_pubkey"); auto op=req.params.find("operator_pubkey");
    if (pk==req.params.end()||op==req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing aggregator_pubkey/operator_pubkey");
    phantomx::aggregator::AggregatorRecord r;
    if (!parse_id32(pk->second,r.aggregator_pubkey)||!parse_id32(op->second,r.operator_pubkey)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid pubkey hex");
    r.endpoint = req.params.count("endpoint")?req.params.at("endpoint"):"";
    auto res = aggregator.registry().register_aggregator(r, static_cast<std::uint64_t>(std::time(nullptr)));
    if (!res.ok()) return err(req.id, RpcErrorCode::kInvalidParams, res.error().to_string());
    return ok(req.id, {{"status","registered"},{"aggregator_pubkey",pk->second}});
  });
  server.register_method("aggregator_list", [&aggregator](const RpcRequest& req) {
    (void)req;
    return ok(req.id, {{"status","ok"},{"certificates",std::to_string(aggregator.certificate_hashes().size())},{"disputes",std::to_string(aggregator.disputes().size())}});
  });
  server.register_method("aggregator_set_status", [&aggregator, parse_id32](const RpcRequest& req) {
    auto pk=req.params.find("aggregator_pubkey"); auto st=req.params.find("status");
    if (pk==req.params.end()||st==req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing aggregator_pubkey/status");
    std::array<std::uint8_t,32> id{}; if (!parse_id32(pk->second,id)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid pubkey");
    phantomx::aggregator::AggregatorStatus as = phantomx::aggregator::AggregatorStatus::kActive;
    if (st->second=="offline") as = phantomx::aggregator::AggregatorStatus::kOffline;
    else if (st->second=="suspended") as = phantomx::aggregator::AggregatorStatus::kSuspended;
    else if (st->second=="banned") as = phantomx::aggregator::AggregatorStatus::kBanned;
    auto r = aggregator.registry().set_status(id, as, static_cast<std::uint64_t>(std::time(nullptr)));
    if (!r.ok()) return err(req.id, RpcErrorCode::kInvalidParams, r.error().to_string());
    return ok(req.id, {{"status","ok"},{"aggregator_pubkey",pk->second},{"new_status",st->second}});
  });
  server.register_method("aggregator_open_dispute", [&aggregator, parse_id32](const RpcRequest& req) {
    auto aid=req.params.find("assignment_id"); auto rep=req.params.find("reporter_pubkey"); auto reason=req.params.find("reason");
    if (aid==req.params.end()||rep==req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing assignment_id/reporter_pubkey");
    std::array<std::uint8_t,32> a{},rp{}; if (!parse_id32(aid->second,a)||!parse_id32(rep->second,rp)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid hex");
    auto d = aggregator.open_dispute(a, rp, reason==req.params.end()?"":reason->second, static_cast<std::uint64_t>(std::time(nullptr)));
    if (!d.ok()) return err(req.id, RpcErrorCode::kInvalidParams, d.error().to_string());
    return ok(req.id, {{"status","open"},{"dispute_id",to_hex_local(d.value().dispute_id)}});
  });
  server.register_method("aggregator_resolve_dispute", [&aggregator, parse_id32](const RpcRequest& req) {
    auto did=req.params.find("dispute_id"); auto st=req.params.find("status");
    if (did==req.params.end()||st==req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing dispute_id/status");
    std::array<std::uint8_t,32> id{}; if (!parse_id32(did->second,id)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid dispute_id");
    phantomx::aggregator::DisputeStatus ds = phantomx::aggregator::DisputeStatus::kOpen;
    if (st->second=="accepted") ds = phantomx::aggregator::DisputeStatus::kAccepted;
    else if (st->second=="rejected") ds = phantomx::aggregator::DisputeStatus::kRejected;
    auto r = aggregator.resolve_dispute(id, ds);
    if (!r.ok()) return err(req.id, RpcErrorCode::kInvalidParams, r.error().to_string());
    return ok(req.id, {{"status","ok"},{"dispute_id",did->second},{"new_status",st->second}});
  });
  server.register_method("aggregator_disputes", [&aggregator](const RpcRequest& req) {
    (void)req;
    return ok(req.id, {{"status","ok"},{"count",std::to_string(aggregator.disputes().size())}});
  });
  server.register_method("aggregator_register_assignment", [&aggregator, parse_id32](const RpcRequest& req) {
    auto aid=req.params.find("assignment_id"); auto sid=req.params.find("subject_id"); auto ag=req.params.find("aggregator_pubkey");
    if (aid==req.params.end()||sid==req.params.end()||ag==req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing assignment_id/subject_id/aggregator_pubkey");
    phantomx::aggregator::AssignmentRecord r;
    if (!parse_id32(aid->second,r.assignment_id)||!parse_id32(sid->second,r.subject_id)||!parse_id32(ag->second,r.aggregator_pubkey))
      return err(req.id, RpcErrorCode::kInvalidParams, "invalid hex");
    r.created_at = static_cast<std::uint64_t>(std::time(nullptr));
    r.expires_at = r.created_at + 3600;
    auto res = aggregator.register_assignment(r);
    if (!res.ok()) return err(req.id, RpcErrorCode::kInvalidParams, res.error().to_string());
    return ok(req.id, {{"status","ok"},{"assignment_id",aid->second}});
  });
  server.register_method("aggregator_get_assignment", [&aggregator, parse_id32](const RpcRequest& req) {
    auto aid=req.params.find("assignment_id"); if (aid==req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing assignment_id");
    std::array<std::uint8_t,32> a{}; if (!parse_id32(aid->second,a)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid assignment_id");
    const auto* r = aggregator.get_assignment(a);
    if (!r) return err(req.id, RpcErrorCode::kNotFound, "assignment not found");
    return ok(req.id, {{"status","ok"},{"assignment_id",aid->second},{"subject_id",to_hex_local(r->subject_id)},{"aggregator_pubkey",to_hex_local(r->aggregator_pubkey)},{"active",r->active?"true":"false"}});
  });
  server.register_method("aggregator_rotate_assignment", [&aggregator, parse_id32](const RpcRequest& req) {
    auto aid=req.params.find("assignment_id"); auto ag=req.params.find("new_aggregator_pubkey");
    if (aid==req.params.end()||ag==req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing assignment_id/new_aggregator_pubkey");
    std::array<std::uint8_t,32> a{},np{}; if (!parse_id32(aid->second,a)||!parse_id32(ag->second,np)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid hex");
    auto r = aggregator.rotate_assignment(a,np,static_cast<std::uint64_t>(std::time(nullptr)));
    if (!r.ok()) return err(req.id, RpcErrorCode::kInvalidParams, r.error().to_string());
    return ok(req.id, {{"status","ok"},{"assignment_id",aid->second}});
  });
  server.register_method("getaggregatorinfo", [&aggregator, parse_id32](const RpcRequest& req) {
    auto aid = req.params.find("assignment_id");
    if (aid != req.params.end()) {
      std::array<std::uint8_t,32> a{}; if (!parse_id32(aid->second,a)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid assignment_id");
      return ok(req.id, {{"status","ok"},{"assignment_id",aid->second},{"attestation_count",std::to_string(aggregator.attestation_count(a))}});
    }
    return ok(req.id, {{"status","foundation"},{"engine","AggregationEngine"}});
  });
  server.register_method("submitattestation", [&aggregator, parse_id32](const RpcRequest& req) {
    auto need = [&](const char* k)->const std::string*{ auto it=req.params.find(k); return it==req.params.end()?nullptr:&it->second; };
    const auto* vpk = need("verifier_pubkey"); const auto* aid = need("assignment_id"); const auto* sid = need("subject_id");
    const auto* evh = need("evidence_hash"); const auto* sig = need("signature");
    if (!vpk||!aid||!sid||!evh||!sig) return err(req.id, RpcErrorCode::kInvalidParams, "missing verifier_pubkey/assignment_id/subject_id/evidence_hash/signature");
    phantomx::aggregator::VerifierAttestation a;
    if (!parse_id32(*vpk,a.verifier_pubkey)||!parse_id32(*aid,a.assignment_id)||!parse_id32(*sid,a.subject_id)||!parse_id32(*evh,a.evidence_hash))
      return err(req.id, RpcErrorCode::kInvalidParams, "invalid hex id/hash");
    std::vector<std::uint8_t> sighex; if (!from_hex(*sig, sighex) || sighex.size()!=64) return err(req.id, RpcErrorCode::kInvalidParams, "invalid signature");
    std::copy(sighex.begin(), sighex.end(), a.signature.begin());
    a.result = (req.params.count("result") && req.params.at("result")=="accept") ? phantomx::aggregator::AttestationResult::kAccept : phantomx::aggregator::AttestationResult::kReject;
    a.timestamp = req.params.count("timestamp") ? std::stoull(req.params.at("timestamp")) : static_cast<std::uint64_t>(std::time(nullptr));
    a.expires_at = req.params.count("expires_at") ? std::stoull(req.params.at("expires_at")) : a.timestamp + 3600;
    auto r = aggregator.submit_attestation({req.params.count("network_id")?req.params.at("network_id"):"regtest", req.params.count("epoch_id")?std::stoull(req.params.at("epoch_id")):0ULL, a.assignment_id, a.subject_id, phantomx::aggregator::SubjectType::kPoiEvidence, a.evidence_hash, {2}}, a, static_cast<std::uint64_t>(std::time(nullptr)));
    if (!r.ok()) return err(req.id, RpcErrorCode::kInvalidParams, r.error().to_string());
    return ok(req.id, {{"status","accepted"},{"assignment_id",*aid}});
  });
  server.register_method("buildcertificate", [&aggregator, parse_id32, aggregator_state_path, aggregator_db_path](const RpcRequest& req) {
    auto need = [&](const char* k)->const std::string*{ auto it=req.params.find(k); return it==req.params.end()?nullptr:&it->second; };
    const auto* aid=need("assignment_id"); const auto* sid=need("subject_id"); const auto* evh=need("evidence_hash"); const auto* agpk=need("aggregator_pubkey"); const auto* agsk=need("aggregator_secret_hex");
    if (!aid||!sid||!evh||!agpk||!agsk) return err(req.id, RpcErrorCode::kInvalidParams, "missing assignment_id/subject_id/evidence_hash/aggregator_pubkey/aggregator_secret_hex");
    phantomx::aggregator::AggregationRequest ar;
    ar.network_id = req.params.count("network_id")?req.params.at("network_id"):"regtest";
    ar.epoch_id = req.params.count("epoch_id")?std::stoull(req.params.at("epoch_id")):0ULL;
    if (!parse_id32(*aid,ar.assignment_id)||!parse_id32(*sid,ar.subject_id)||!parse_id32(*evh,ar.evidence_hash)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid hex id/hash");
    std::array<std::uint8_t,32> agpub{}; if (!parse_id32(*agpk,agpub)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid aggregator_pubkey");
    auto sk = phantomx::crypto::parse_secret_key_hex(*agsk);
    if (!sk.ok()) return err(req.id, RpcErrorCode::kInvalidParams, "invalid aggregator_secret_hex");
    auto cert = aggregator.build_certificate(ar, sk.value(), agpub, static_cast<std::uint64_t>(std::time(nullptr)));
    if (!cert.ok()) return err(req.id, RpcErrorCode::kInvalidParams, cert.error().to_string());
    auto bh = phantomx::aggregator::certificate_hash(cert.value());
    if (!aggregator_db_path.empty()) (void)aggregator.save_state_db(aggregator_db_path);
    if (!aggregator_state_path.empty()) (void)aggregator.save_state(aggregator_state_path);
    return ok(req.id, {{"status","ok"},{"certificate_hash", to_hex_local(bh)}, {"quorum_achieved", std::to_string(cert.value().quorum_achieved)}, {"result", cert.value().result==phantomx::aggregator::AttestationResult::kAccept?"accept":"reject"}});
  });
  server.register_method("getaggregation", [&aggregator, parse_id32](const RpcRequest& req) {
    auto it=req.params.find("assignment_id"); if (it==req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing assignment_id");
    std::array<std::uint8_t,32> a{}; if (!parse_id32(it->second,a)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid assignment_id");
    return ok(req.id, {{"assignment_id",it->second},{"attestation_count",std::to_string(aggregator.attestation_count(a))}});
  });
  server.register_method("getcertificate", [&aggregator, parse_id32](const RpcRequest& req) {
    auto it=req.params.find("certificate_hash");
    if (it==req.params.end()) {
      auto hashes = aggregator.certificate_hashes();
      std::string list="[";
      for (size_t i=0;i<hashes.size();++i){ if(i) list+=","; list+="""+to_hex_local(hashes[i])+"""; }
      list+="]";
      return ok(req.id, {{"status","ok"},{"count",std::to_string(hashes.size())},{"certificate_hashes",list}});
    }
    Hash256Bytes h{}; if (!parse_id32(it->second,h)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid certificate_hash");
    const auto* cert = aggregator.get_certificate(h);
    if (!cert) return err(req.id, RpcErrorCode::kNotFound, "certificate not found");
    return ok(req.id, {{"status","ok"},{"certificate_hash",it->second},{"assignment_id",to_hex_local(cert->assignment_id)},{"subject_id",to_hex_local(cert->subject_id)},{"result",cert->result==phantomx::aggregator::AttestationResult::kAccept?"accept":"reject"},{"quorum_required",std::to_string(cert->quorum_required)},{"quorum_achieved",std::to_string(cert->quorum_achieved)},{"aggregator_pubkey",to_hex_local(cert->aggregator_pubkey)}});
  });
  server.register_method("verifycertificate", [&aggregator, parse_id32](const RpcRequest& req) {
    auto it=req.params.find("certificate_hash");
    if (it!=req.params.end()) {
      Hash256Bytes h{}; if (!parse_id32(it->second,h)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid certificate_hash");
      const auto* cert = aggregator.get_certificate(h);
      if (!cert) return ok(req.id, {{"valid","false"},{"reason","certificate not found"}});
      bool v = aggregator.verify_certificate(*cert);
      return ok(req.id, {{"valid", v?"true":"false"}, {"mode","stored_certificate"}, {"certificate_hash",it->second}});
    }
    // fallback: signature-only check
    auto sig=req.params.find("aggregator_signature"); auto pk=req.params.find("aggregator_pubkey");
    if (sig==req.params.end()||pk==req.params.end()) return ok(req.id, {{"status","foundation"},{"valid","false"},{"reason","provide certificate_hash or signature+pubkey"}});
    std::vector<std::uint8_t> sh,pkh; Hash256Bytes hh{};
    auto ch=req.params.find("certificate_hash_hex"); if (ch==req.params.end() || !parse_id32(ch->second,hh)) return ok(req.id, {{"valid","false"},{"reason","missing/invalid certificate_hash_hex"}});
    if (!from_hex(sig->second,sh)||sh.size()!=64||!from_hex(pk->second,pkh)||pkh.size()!=32) return ok(req.id, {{"valid","false"},{"reason","invalid hex"}});
    std::array<std::uint8_t,64> sarr{}; std::copy(sh.begin(),sh.end(),sarr.begin());
    std::array<std::uint8_t,32> parr{}; std::copy(pkh.begin(),pkh.end(),parr.begin());
    bool v = phantomx::crypto::verify(std::span<const std::uint8_t>(hh.data(),hh.size()), phantomx::crypto::Signature{sarr}, phantomx::crypto::PublicKey{parr});
    return ok(req.id, {{"valid", v?"true":"false"}, {"mode","signature_only_foundation"}});
  });

  // Consensus (PoI)
  server.register_method("getvalidatorinfo", [](const RpcRequest& req) {
    return ok(req.id, {{"status","ok"},{"mode","poi"},{"validators","0"},{"total_voting_power","0"},{"note","BFT validators removed; use verifier registry and getpoiinfo"}});
  });
  server.register_method("getconsensusinfo", [&chain](const RpcRequest& req) {
    auto tip = chain.tip();
    return ok(req.id, {
      {"consensus","poi"},
      {"consensus_mode","poi"},
      {"height", tip.ok()&&tip.value().has_value()?std::to_string(tip.value()->height):"0"},
      {"finality_model","poi_attested"},
      {"finality_quorum",std::to_string(g_poi_finality_quorum.load())}
    });
  });
  server.register_method("getfinalityinfo", [&chain](const RpcRequest& req) {
    auto tip = chain.tip();
    const auto tip_h = tip.ok()&&tip.value().has_value()?tip.value()->height:0;

    // PoI finality lives in these in-memory globals; BFT finality is persisted
    // to chain metadata by the daemon. Report whichever commitment is further
    // ahead so clients see the authoritative height/hash regardless of engine.
    std::uint64_t fh = g_poi_finalized_height.load();
    std::string fhash;
    {
      std::scoped_lock lk(g_poi_mu);
      fhash = g_poi_finalized_hash;
    }
    bool bft_ahead = false;
    auto meta_h = chain.get_metadata("finalized_height");
    if (meta_h.ok() && meta_h.value().has_value()) {
      std::uint64_t mh = 0;
      try { mh = std::stoull(*meta_h.value()); } catch (...) { mh = 0; }
      if (mh > fh) {
        fh = mh;
        auto meta_hash = chain.get_metadata("finalized_hash");
        if (meta_hash.ok() && meta_hash.value().has_value()) fhash = *meta_hash.value();
        bft_ahead = true;
      }
    }

    std::unordered_map<std::string,std::string> r{{"chain_height", std::to_string(tip_h)}};
    r["status"]="ok";
    r["consensus_mode"]= bft_ahead ? "bft" : "poi";
    r["finalized_height"]= std::to_string(fh);
    r["finalized_hash"]= fhash;
    if (!bft_ahead) {
      std::scoped_lock lk(g_poi_mu);
      auto it = g_poi_block_finality.find(fhash);
      if (it != g_poi_block_finality.end()) {
        r["finalized_attestations"]=std::to_string(it->second.attestations.size());
        r["finalized_quorum_required"]=std::to_string(it->second.quorum_required);
        r["finalized_at"]=std::to_string(it->second.finalized_at);
      }
    }
    r["finality_model"]= bft_ahead ? "bft_qc" : "poi_attested";
    r["finality_quorum"]=std::to_string(g_poi_finality_quorum.load());
    r["q_min"]=std::to_string(g_poi_q_min.load());
    r["verifiers_online"]=std::to_string(g_poi_verifiers_online.load());
    r["finality_status"] = (fh >= tip_h && tip_h > 0) ? "finalized" : (fh == 0 && tip_h == 0 ? "genesis" : "pending_attestation");
    return ok(req.id, r);
  });
  server.register_method("getquorumcertificate", [](const RpcRequest& req) {
    return ok(req.id, {{"status","poi"}, {"available","false"}, {"note","BFT quorum removed; use getfinalityinfo/getpoiinfo for PoI finality."}});
  });

  server.register_method("bft_status", [bft](const RpcRequest& req) {
    if (!bft) return ok(req.id, {{"status","bft_disabled"}});
    return ok(req.id, {{"status","ok"},{"epoch",std::to_string(bft->state().epoch)},{"view",std::to_string(bft->state().view)},{"height",std::to_string(bft->state().height)},{"locked", bft->state().locked_hash.has_value()?"true":"false"},{"equivocations",std::to_string(bft->equivocations().size())},{"slashed","0"}});
  });
  server.register_method("bft_beacon", [bft](const RpcRequest& req) {
    if (!bft) return ok(req.id, {{"status","bft_disabled"}});
    return ok(req.id, {{"status","foundation"},{"beacon",""},{"warning","randomness beacon unavailable"}});
  });
  server.register_method("bft_submit_vote", [bft,parse_id32](const RpcRequest& req) {
    if (!bft) return err(req.id, RpcErrorCode::kInternalError, "bft disabled");
    auto gp=[&](const char* k)->const std::string*{ auto it=req.params.find(k); return it==req.params.end()?nullptr:&it->second; };
    const auto *ph=gp("phase"),*ep=gp("epoch"),*vw=gp("view"),*ht=gp("height"),*bh=gp("block_hash"),*pk=gp("validator_pubkey"),*vp=gp("voting_power"),*sg=gp("signature");
    if (!ph||!ep||!vw||!ht||!bh||!pk||!vp||!sg) return err(req.id, RpcErrorCode::kInvalidParams, "missing vote fields");
    phantomx::consensus::Vote v;
    v.phase = (*ph=="precommit"||*ph=="2") ? phantomx::consensus::VotePhase::kPrecommit : phantomx::consensus::VotePhase::kPrevote;
    v.epoch=std::stoull(*ep); v.view=std::stoull(*vw); v.height=static_cast<std::uint32_t>(std::stoul(*ht)); v.voting_power=std::stoull(*vp);
    if (!parse_id32(*bh,v.block_hash)||!parse_id32(*pk,v.validator_pubkey)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid hash/pubkey hex");
    std::vector<std::uint8_t> sb; if(!from_hex(*sg,sb)||sb.size()!=64) return err(req.id, RpcErrorCode::kInvalidParams, "invalid signature hex");
    std::copy(sb.begin(),sb.end(),v.signature.begin());
    auto r = bft->submit_vote(v);
    if (!r.ok()) return err(req.id, RpcErrorCode::kInvalidParams, r.error().to_string());
    auto qc = bft->maybe_qc(v.phase,v.epoch,v.view,v.block_hash);
    return ok(req.id, {{"status","accepted"},{"qc_formed", qc.has_value()?"true":"false"}});
  });
  server.register_method("bft_pacemaker_tick", [bft](const RpcRequest& req) {
    if (!bft) return err(req.id, RpcErrorCode::kInternalError, "bft disabled");
    std::uint64_t now_ms = req.params.count("now_ms") ? std::stoull(req.params.at("now_ms")) : static_cast<std::uint64_t>(std::time(nullptr))*1000ULL;
    (void)now_ms;
    return ok(req.id, {{"status","foundation"},{"view",std::to_string(bft->state().view)}});
  });
  server.register_method("bft_local_pubkey", [local_bft_key](const RpcRequest& req) {
    if (!local_bft_key) return ok(req.id, {{"status","no_local_key"}});
    return ok(req.id, {{"status","ok"},{"pubkey", to_hex_local(local_bft_key->pub.bytes)}});
  });
  server.register_method("bft_selected_proposer", [bft, proposer_selector](const RpcRequest& req) {
    if (!bft) return err(req.id, RpcErrorCode::kInternalError, "bft disabled");
    const auto parse_u64 = [&req](const char* name, std::uint64_t fallback) {
      const auto it = req.params.find(name);
      return it == req.params.end() ? fallback : std::stoull(it->second);
    };
    const auto epoch = parse_u64("epoch", bft->state().epoch);
    const auto view = parse_u64("view", bft->state().view);
    const auto proposer = proposer_selector(epoch, view);
    return ok(req.id, {{"status","ok"}, {"epoch",std::to_string(epoch)},
                       {"view",std::to_string(view)}, {"proposer_pubkey",to_hex_local(proposer)}});
  });
  server.register_method("bft_add_validator", [bft,parse_id32](const RpcRequest& req) {
    if (!bft) return err(req.id, RpcErrorCode::kInternalError, "bft disabled");
    auto pk=req.params.find("pubkey"); if (pk==req.params.end()) pk=req.params.find("--pubkey"); if (pk==req.params.end()) return err(req.id, RpcErrorCode::kInvalidParams, "missing pubkey");
    std::array<std::uint8_t,32> pub{}; if(!parse_id32(pk->second,pub)) return err(req.id, RpcErrorCode::kInvalidParams, "invalid pubkey");
    auto vit=req.params.find("voting_power"); if(vit==req.params.end()) vit=req.params.find("--voting_power"); std::uint64_t vp = vit!=req.params.end() ? std::stoull(vit->second) : 10;
    auto vs = bft->validators();
    bool found=false;
    for (auto& v : vs.validators) if (v.validator_pubkey == pub) { v.voting_power=vp; v.active=true; found=true; break; }
    if (!found) { phantomx::consensus::Validator v; v.validator_pubkey=pub; v.operator_pubkey=pub; v.voting_power=vp; v.active=true; vs.validators.push_back(v); }
    vs.total_voting_power = 0; for (const auto& vv : vs.validators) if (vv.active) vs.total_voting_power += vv.voting_power;
    bft->rotate_validators(vs);
    return ok(req.id, {{"status","added"},{"validators",std::to_string(bft->validators().validators.size())}});
  });
  server.register_method("bft_propose", [&chain,bft,p2p,local_bft_key,on_local_proposal,proposer_selector](const RpcRequest& req) {
    if (!bft || !local_bft_key || !p2p) return err(req.id, RpcErrorCode::kInternalError, "bft, p2p or local key disabled");
    auto tip = chain.tip();
    if (!tip.ok()) return err(req.id, RpcErrorCode::kInternalError, tip.error().to_string());
    const auto height = tip.value().has_value() ? tip.value()->height + 1 : 0;
    const auto selected = proposer_selector(bft->state().epoch, bft->state().view);
    if (selected != local_bft_key->pub.bytes)
      return err(req.id, RpcErrorCode::kInvalidParams, "local validator is not selected proposer");
    phantomx::Block block;
    block.header.height = height;
    block.header.timestamp = static_cast<std::uint64_t>(std::time(nullptr));
    if (tip.value().has_value()) block.header.prev_hash = tip.value()->hash;
    phantomx::Transaction coinbase;
    coinbase.kind = phantomx::kTxKindCoinbase;
    phantomx::TxIn coinbase_input;
    coinbase_input.prevout.txid = {};
    coinbase_input.prevout.vout = 0xffffffffu;
    coinbase_input.script_sig = {static_cast<std::uint8_t>(height & 0xffu)};
    coinbase_input.sequence = 0xffffffffu;
    coinbase.inputs.push_back(std::move(coinbase_input));
    phantomx::TxOut coinbase_output;
    coinbase_output.amount_atoms = phantomx::rewards::block_subsidy_atoms(height);
    coinbase_output.script_pubkey = {0x51};
    coinbase.outputs.push_back(std::move(coinbase_output));
    block.transactions.push_back(std::move(coinbase));
    std::vector<phantomx::Hash256Bytes> txids;
    txids.reserve(block.transactions.size());
    for (const auto& tx : block.transactions) txids.push_back(phantomx::txid(tx));
    block.header.merkle_root = phantomx::merkle_root(std::move(txids));
    auto valid = chain.validate_block(block);
    if (!valid.ok()) return err(req.id, RpcErrorCode::kInvalidParams, valid.error().to_string());
    const auto hash = phantomx::block_hash(block.header);
    auto stored = chain.write_block(block);
    if (!stored.ok()) return err(req.id, RpcErrorCode::kInternalError, stored.error().to_string());
    phantomx::consensus::BlockProposal p;
    p.epoch=bft->state().epoch; p.view=bft->state().view; p.height=height;
    p.block_hash=hash; p.parent_hash=block.header.prev_hash; p.proposer_pubkey=local_bft_key->pub.bytes; p.timestamp=block.header.timestamp;
    p.justify_qc = bft->state().valid_qc;
    auto signed_p=phantomx::consensus::sign_proposal(p,local_bft_key->sec);
    if (!signed_p.ok()) return err(req.id,RpcErrorCode::kInternalError,signed_p.error().to_string());
    p=signed_p.value();
    (void)p2p->broadcast_bft_block(block);
    auto broadcast = p2p->broadcast_bft_proposal(p);
    if (!broadcast.ok()) return err(req.id, RpcErrorCode::kInternalError, broadcast.error().to_string());
    if (on_local_proposal) on_local_proposal(p);
    return ok(req.id, {{"status","proposed"},{"block_hash",to_hex_local(hash)},{"height",std::to_string(height)}});
  });

  // Rewards
  server.register_method("getblockreward", [](const RpcRequest& req) {
    auto h = req.params.count("height") ? std::stoull(req.params.at("height")) : 0ULL;
    const auto subsidy = phantomx::rewards::block_subsidy_atoms(h);
    const auto ver = phantomx::rewards::bps_share(subsidy, phantomx::rewards::kVerifierShareBps);
    const auto poi = subsidy - ver;
    return ok(req.id, {{"height",std::to_string(h)},{"subsidy_atoms",std::to_string(subsidy)},{"poi_atoms",std::to_string(poi)},{"verifier_atoms",std::to_string(ver)}});
  });
  server.register_method("getrewardinfo", [](const RpcRequest& req) {
    return ok(req.id, {{"distribution","90/10"},{"contributor_percent","90"},{"verifier_percent","10"},{"status","foundation"}});
  });
  server.register_method("getepochrewards", [](const RpcRequest& req) {
    auto e = req.params.count("epoch") ? req.params.at("epoch") : "0"; return ok(req.id, {{"epoch",e},{"status","foundation"}});
  });

  server.register_method("backupwallet", [](const RpcRequest& req) {
    return ok(req.id, {{"status", "foundation: no-op"}});
  });

  // PoI-driven block producer. When PoI is running, produce blocks at an interval
  // modulated by epoch/score (target_block_interval_ms).
  // Only start when a P2P node is attached: the thread captures references to the
  // caller's chain/mempool, so it must not outlive them (e.g. in unit tests that
  // register handlers with a stack-local ChainDatabase).
  bool expected = false;
  if (p2p != nullptr && g_poi_producer_started.compare_exchange_strong(expected, true)) {
    std::thread([&chain, p2p, mine_one, save_poi_attestations, count_online_verifiers, poi_required_quorum]() mutable {
      using namespace std::chrono_literals;
      std::uint64_t last_produced_ms = 0;
      std::uint64_t last_rebroadcast_ms = 0;
      while (true) {
        std::this_thread::sleep_for(1s);
        if (!g_poi_running.load()) continue;

        const auto now_sec = static_cast<std::uint64_t>(std::time(nullptr));
        const auto now_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        // Periodic quorum refresh + finality check (daemon-side, outside heartbeat/RPC).
        {
          const auto online = count_online_verifiers();
          g_poi_verifiers_online.store(online);
          const auto quorum = poi_required_quorum(online);
          g_poi_finality_quorum.store(quorum);
          bool changed = false;
          std::scoped_lock lk(g_poi_mu);
          for (auto& [_, bf] : g_poi_block_finality) {
            if (bf.quorum_required != quorum && !bf.finalized) { bf.quorum_required = quorum; changed = true; }
            if (!bf.finalized && bf.attestations.size() >= bf.quorum_required) {
              bf.finalized = true;
              bf.finalized_at = now_sec;
              if (bf.height >= g_poi_finalized_height.load()) {
                g_poi_finalized_height.store(bf.height);
                g_poi_finalized_hash = bf.block_hash;
              }
              (void)chain.set_metadata("finalized_height", std::to_string(g_poi_finalized_height.load()));
              (void)chain.set_metadata("finalized_hash", g_poi_finalized_hash);
              changed = true;
            }
          }
          if (changed) save_poi_attestations();
        }

        // Retry gossip: ACK-driven pending retry + periodic rebroadcast fallback.
        if (p2p) p2p->retry_pending_poi_attestations(now_ms);
        if (p2p && (last_rebroadcast_ms == 0 || now_ms >= last_rebroadcast_ms + 30000ULL)) {
          last_rebroadcast_ms = now_ms;
          std::vector<std::string> payloads;
          {
            std::scoped_lock lk(g_poi_mu);
            for (const auto& [_, bf] : g_poi_block_finality) {
              if (bf.finalized) continue;
              for (const auto& a : bf.attestations) {
                json_t* gp = json_object();
                json_object_set_new(gp, "type", json_string("poi_attestation"));
                json_object_set_new(gp, "verifier_pubkey", json_string(a.verifier_pubkey_hex.c_str()));
                json_object_set_new(gp, "block_hash", json_string(bf.block_hash.c_str()));
                json_object_set_new(gp, "height", json_integer(static_cast<json_int_t>(bf.height)));
                json_object_set_new(gp, "timestamp", json_integer(static_cast<json_int_t>(a.timestamp)));
                json_object_set_new(gp, "signature", json_string(a.signature_hex.c_str()));
                char* gt = json_dumps(gp, JSON_COMPACT);
                if (gt) { payloads.emplace_back(gt); free(gt); }
                json_decref(gp);
              }
            }
          }
          for (const auto& pl : payloads) (void)p2p->broadcast_poi_attestation(pl);
        }

        if (!g_poi_autoproduce.load()) continue;

        const auto active_miners = poi_active_miners(now_sec);
        const auto epoch = poi_epoch(active_miners);
        // Compute avg score from active participants
        const double measured_latency = poi_latency_ms(p2p);
        const double measured_relay = poi_relay_ratio(p2p);
        double avg_score = 0.0;
        std::uint64_t scored = 0;
        {
          std::scoped_lock lk(g_poi_mu);
          for (const auto& [_, p] : g_poi_participants) {
            if (!p.running || p.last_heartbeat == 0) continue;
            if (now_sec < p.last_heartbeat || (now_sec - p.last_heartbeat) > kPoiHeartbeatWindowSec) continue;
            const double lms = p.latency_ms >= 0.0 ? p.latency_ms : measured_latency;
            const double rr = p.relay_ratio >= 0.0 ? p.relay_ratio : measured_relay;
            const double up = poi_uptime_ratio(p, now_sec);
            auto sr = poi_compute_score(p.bandwidth_bytes, p.bandwidth_unit, lms, rr, up, active_miners);
            if (sr.available) { avg_score += sr.score; ++scored; }
          }
        }
        if (scored == 0 && g_poi_running.load()) {
          auto sr = poi_compute_score(g_poi_bandwidth_bytes.load(), g_poi_bandwidth_unit, measured_latency, measured_relay, -1.0, active_miners);
          if (sr.available) { avg_score = sr.score; scored = 1; }
        }
        if (scored > 0) avg_score /= static_cast<double>(scored);

        const auto target_ms = poi_target_block_interval_ms(60000ULL, avg_score, epoch);
        if (last_produced_ms != 0 && now_ms < last_produced_ms + target_ms) continue;

        // PoI production (non-BFT): mine block via mine_one
        std::string reward;
        { std::scoped_lock lk(g_poi_mu); reward = g_poi_reward_address; }
        if (reward.empty()) {
          try {
            const auto net = phantomx::Network::kRegtest;
            const auto dd = phantomx::default_datadir(net);
            const auto keystore = (dd / "wallet" / "wallet.keystore").string();
            phantomx::wallet::Wallet w(keystore, "http://127.0.0.1:8741");
            w.set_network("regtest");
            if (const char* tok = std::getenv("PHANTOMX_RPC_TOKEN"); tok && *tok) w.set_rpc_token(tok);
            (void)w.open();
            auto labels = w.account_labels();
            if (!labels.empty()) reward = w.address_for(labels.front());
          } catch (...) {}
        }
        if (!reward.empty()) {
          auto mined = mine_one(reward, false);
          if (mined.ok()) {
            g_poi_blocks_found.fetch_add(1);
            g_poi_last_block_at.store(static_cast<std::uint64_t>(std::time(nullptr)));
            const auto bh = to_hex_local(mined.value());
            std::uint64_t bh_height = 0;
            auto tipr = chain.tip();
            if (tipr.ok() && tipr.value().has_value()) bh_height = tipr.value()->height;
            {
              std::scoped_lock lk(g_poi_mu);
              g_poi_last_block_hash = bh;
              const auto online = g_poi_verifiers_online.load();
              const auto quorum = poi_required_quorum(online);
              auto& bf = g_poi_block_finality[bh];
              bf.height = bh_height;
              bf.block_hash = bh;
              bf.quorum_required = quorum;
              if (!bf.finalized && (bf.attestations.size() >= quorum || (online == 0 && g_poi_q_min.load() <= 1))) {
                bf.finalized = true;
                bf.finalized_at = static_cast<std::uint64_t>(std::time(nullptr));
                g_poi_finalized_height.store(bh_height);
                g_poi_finalized_hash = bh;
                (void)chain.set_metadata("finalized_height", std::to_string(bh_height));
                (void)chain.set_metadata("finalized_hash", bh);
              }
              g_poi_finality_quorum.store(quorum);
            }
            save_poi_attestations();
          }
        }
        last_produced_ms = now_ms;
      }
    }).detach();
  }

  return Result<void>{};
}

}  // namespace phantomx::rpc
