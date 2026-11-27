#include "phantomx/poi/anti_sybil/anti_sybil.hpp"

#include <algorithm>
#include <unordered_set>

#include "phantomx/primitives/hash.hpp"

namespace phantomx::poi {
namespace {
std::string hex_key(const std::array<std::uint8_t, 32>& pk) {
  return to_hex(std::span<const std::uint8_t>(pk.data(), pk.size()));
}
}  // namespace

std::string AntiSybilRegistry::endpoint_prefix(const std::string& endpoint) {
  // Very simple prefix extraction for diversity signal only; NOT identity.
  // IPv4: first 3 octets, IPv6/host: first token up to ':' or '.' group.
  auto colon = endpoint.rfind(':');
  std::string host = colon == std::string::npos ? endpoint : endpoint.substr(0, colon);
  int dots = 0;
  for (std::size_t i = 0; i < host.size(); ++i) {
    if (host[i] == '.') {
      ++dots;
      if (dots == 3) return host.substr(0, i);
    }
  }
  return host;
}

std::string AntiSybilRegistry::pubkey_hex(const std::array<std::uint8_t, 32>& pk) { return hex_key(pk); }

Result<void> AntiSybilRegistry::register_identity(const NodeIdentity& id, std::uint64_t now) {
  if (!verify_identity(id)) return MakeError(ErrorCode::kInvalidArgument, "invalid identity signature");
  if (!identity_fresh(id, now, cfg_.identity_max_age_seconds)) return MakeError(ErrorCode::kInvalidArgument, "identity not fresh");
  const auto key = hex_key(id.node_pubkey);
  if (records_.find(key) != records_.end()) return MakeError(ErrorCode::kInvalidArgument, "duplicate identity");

  // operator diversity limit
  std::size_t op_count = 0;
  for (const auto& [_, r] : records_) {
    if (r.identity.operator_pubkey == id.operator_pubkey) ++op_count;
  }
  if (op_count >= cfg_.max_nodes_per_operator) return MakeError(ErrorCode::kInvalidArgument, "operator node cap exceeded");

  RegistrationRecord rec;
  rec.identity = id;
  rec.last_seen = now;
  records_[key] = std::move(rec);
  return Result<void>{};
}

Result<void> AntiSybilRegistry::rotate_identity(const IdentityRotation& rotation, const NodeIdentity& new_identity, std::uint64_t now) {
  if (!verify_rotation(rotation)) return MakeError(ErrorCode::kInvalidArgument, "invalid rotation signature");
  if (!verify_identity(new_identity)) return MakeError(ErrorCode::kInvalidArgument, "invalid new identity signature");
  if (new_identity.node_pubkey != rotation.new_node_pubkey) return MakeError(ErrorCode::kInvalidArgument, "new identity pubkey mismatch");
  if (!identity_fresh(new_identity, now, cfg_.identity_max_age_seconds)) return MakeError(ErrorCode::kInvalidArgument, "new identity not fresh");
  const auto old_key = hex_key(rotation.old_node_pubkey);
  auto it = records_.find(old_key);
  if (it == records_.end()) return MakeError(ErrorCode::kNotFound, "old identity not found");
  RegistrationRecord rec = it->second;
  rec.identity = new_identity;
  rec.last_seen = now;
  records_.erase(it);
  records_[hex_key(rotation.new_node_pubkey)] = std::move(rec);
  return Result<void>{};
}

bool AntiSybilRegistry::has_identity(const std::array<std::uint8_t, 32>& node_pubkey) const {
  return records_.find(hex_key(node_pubkey)) != records_.end();
}

const RegistrationRecord* AntiSybilRegistry::find(const std::array<std::uint8_t, 32>& node_pubkey) const {
  auto it = records_.find(hex_key(node_pubkey));
  if (it == records_.end()) return nullptr;
  return &it->second;
}

AntiSybilAssessment AntiSybilRegistry::assess(const NodeIdentity& id, std::uint64_t now) const {
  AntiSybilAssessment a;
  a.node_pubkey = id.node_pubkey;

  if (!verify_identity(id)) {
    a.signals.push_back({"invalid_signature", "identity signature invalid", 100});
  }
  if (!identity_fresh(id, now, cfg_.identity_max_age_seconds)) {
    a.signals.push_back({"stale_identity", "identity expired or too old", 70});
  }

  std::size_t same_operator = 0;
  std::size_t same_endpoint_prefix = 0;
  std::size_t same_wallet = 0;
  const auto prefix = endpoint_prefix(id.network_endpoint);
  for (const auto& [_, r] : records_) {
    if (r.identity.node_pubkey == id.node_pubkey) continue;
    if (r.identity.operator_pubkey == id.operator_pubkey) ++same_operator;
    if (!prefix.empty() && endpoint_prefix(r.identity.network_endpoint) == prefix) ++same_endpoint_prefix;
    if (!id.wallet_address.empty() && r.identity.wallet_address == id.wallet_address) ++same_wallet;
  }
  if (same_operator + 1 > cfg_.max_nodes_per_operator) {
    a.signals.push_back({"operator_cap", "too many nodes for operator", 80});
  }
  if (same_endpoint_prefix + 1 > cfg_.max_nodes_per_endpoint_prefix) {
    a.signals.push_back({"endpoint_diversity", "too many nodes from same endpoint prefix (signal only)", 40});
  }
  if (same_wallet > 0) {
    a.signals.push_back({"wallet_reuse", "wallet address reused across nodes (signal)", 35});
  }

  const auto* rec = find(id.node_pubkey);
  if (rec != nullptr && rec->contribution_units > cfg_.max_contribution_per_node) {
    a.signals.push_back({"contribution_cap_node", "node contribution cap exceeded", 60});
  }

  std::uint64_t op_units = 0;
  for (const auto& [_, r] : records_) {
    if (r.identity.operator_pubkey == id.operator_pubkey) op_units += r.contribution_units;
  }
  if (op_units > cfg_.max_contribution_per_operator) {
    a.signals.push_back({"contribution_cap_operator", "operator contribution cap exceeded", 65});
  }

  std::uint32_t risk = 0;
  for (const auto& s : a.signals) risk = std::min<std::uint32_t>(100, risk + s.severity / 2);
  a.risk_score = risk;
  a.allow_contribution = risk < 70;
  return a;
}

Result<void> AntiSybilRegistry::record_contribution(const std::array<std::uint8_t, 32>& node_pubkey, std::uint64_t units) {
  auto it = records_.find(hex_key(node_pubkey));
  if (it == records_.end()) return MakeError(ErrorCode::kNotFound, "identity not found");
  it->second.contribution_units += units;
  return Result<void>{};
}

bool evidence_fresh(std::uint64_t evidence_timestamp, std::uint64_t now, std::uint64_t max_age_seconds) {
  if (evidence_timestamp > now) return false;
  return (now - evidence_timestamp) <= max_age_seconds;
}

std::vector<SuspicionSignal> detect_collusion_signals(const std::vector<NodeIdentity>& identities) {
  std::vector<SuspicionSignal> signals;
  std::unordered_map<std::string, std::size_t> op_counts;
  std::unordered_map<std::string, std::size_t> ep_counts;
  std::unordered_map<std::string, std::size_t> wallet_counts;
  for (const auto& id : identities) {
    op_counts[hex_key(id.operator_pubkey)]++;
    const auto prefix = AntiSybilRegistry::endpoint_prefix(id.network_endpoint);
    if (!prefix.empty()) ep_counts[prefix]++;
    if (!id.wallet_address.empty()) wallet_counts[id.wallet_address]++;
  }
  for (const auto& [k, v] : op_counts) {
    if (v > 3) signals.push_back({"collusion_operator_cluster", "multiple nodes share operator key: " + k.substr(0, 16), 60});
  }
  for (const auto& [k, v] : ep_counts) {
    if (v > 5) signals.push_back({"collusion_endpoint_cluster", "many nodes share endpoint prefix (signal only): " + k, 35});
  }
  for (const auto& [k, v] : wallet_counts) {
    if (v > 1) signals.push_back({"collusion_wallet_reuse", "wallet reused across nodes", 40});
  }
  return signals;
}

}  // namespace phantomx::poi
