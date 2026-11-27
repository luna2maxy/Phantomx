#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/poi/identity/identity.hpp"
#include "phantomx/poi/types.hpp"

namespace phantomx::poi {

struct RegistrationRecord {
  NodeIdentity identity;
  std::uint64_t last_seen = 0;
  std::uint64_t contribution_units = 0; // fixed-point/units as defined by policy
  bool suspended = false;
};

struct SuspicionSignal {
  std::string code;
  std::string detail;
  std::uint32_t severity = 0; // 0..100
};

struct AntiSybilAssessment {
  std::array<std::uint8_t, 32> node_pubkey{};
  std::uint32_t risk_score = 0; // 0..100
  std::vector<SuspicionSignal> signals;
  bool allow_contribution = true;
};

struct AntiSybilConfig {
  std::size_t max_nodes_per_operator = 5;
  std::size_t max_nodes_per_endpoint_prefix = 10;
  std::uint64_t max_contribution_per_operator = 100000;
  std::uint64_t max_contribution_per_node = 25000;
  std::uint64_t evidence_max_age_seconds = 3600;
  std::uint64_t identity_max_age_seconds = 86400 * 30;
};

class AntiSybilRegistry {
 public:
  explicit AntiSybilRegistry(AntiSybilConfig cfg = {}) : cfg_(cfg) {}

  [[nodiscard]] Result<void> register_identity(const NodeIdentity& id, std::uint64_t now);
  [[nodiscard]] Result<void> rotate_identity(const IdentityRotation& rotation, const NodeIdentity& new_identity, std::uint64_t now);
  [[nodiscard]] bool has_identity(const std::array<std::uint8_t, 32>& node_pubkey) const;
  [[nodiscard]] const RegistrationRecord* find(const std::array<std::uint8_t, 32>& node_pubkey) const;

  [[nodiscard]] AntiSybilAssessment assess(const NodeIdentity& id, std::uint64_t now) const;
  [[nodiscard]] Result<void> record_contribution(const std::array<std::uint8_t, 32>& node_pubkey, std::uint64_t units);

  [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
  [[nodiscard]] const AntiSybilConfig& config() const noexcept { return cfg_; }

  [[nodiscard]] static std::string endpoint_prefix(const std::string& endpoint);

 private:
  static std::string pubkey_hex(const std::array<std::uint8_t, 32>& pk);

  AntiSybilConfig cfg_;
  std::unordered_map<std::string, RegistrationRecord> records_; // key = node pubkey hex
};

[[nodiscard]] bool evidence_fresh(std::uint64_t evidence_timestamp, std::uint64_t now, std::uint64_t max_age_seconds);
[[nodiscard]] std::vector<SuspicionSignal> detect_collusion_signals(const std::vector<NodeIdentity>& identities);

}  // namespace phantomx::poi
