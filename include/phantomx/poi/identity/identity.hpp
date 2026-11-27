#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "phantomx/common/crypto.hpp"
#include "phantomx/common/error.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::poi {

struct NodeIdentity {
  std::array<std::uint8_t, 32> node_pubkey{};
  std::array<std::uint8_t, 32> operator_pubkey{}; // separate from wallet address
  std::string wallet_address;                     // separate from operator identity
  std::string network_endpoint;                   // informational, NOT sole identity
  std::uint64_t registered_at = 0;
  std::uint64_t expires_at = 0;
  std::uint64_t version = 1;
  std::array<std::uint8_t, 32> registration_nonce{};
  std::array<std::uint8_t, 64> signature{};       // signed by node key
};

struct IdentityRotation {
  std::array<std::uint8_t, 32> old_node_pubkey{};
  std::array<std::uint8_t, 32> new_node_pubkey{};
  std::uint64_t rotated_at = 0;
  std::array<std::uint8_t, 32> nonce{};
  std::array<std::uint8_t, 64> signature_by_old{};
  std::array<std::uint8_t, 64> signature_by_new{};
};

[[nodiscard]] std::vector<std::uint8_t> serialize_identity(const NodeIdentity& id);
[[nodiscard]] Hash256Bytes identity_hash(const NodeIdentity& id);
[[nodiscard]] Result<NodeIdentity> sign_identity(NodeIdentity id, const crypto::SecretKey& node_secret);
[[nodiscard]] bool verify_identity(const NodeIdentity& id);
[[nodiscard]] bool identity_fresh(const NodeIdentity& id, std::uint64_t now, std::uint64_t max_age_seconds);

[[nodiscard]] std::vector<std::uint8_t> serialize_rotation(const IdentityRotation& r);
[[nodiscard]] Hash256Bytes rotation_hash(const IdentityRotation& r);
[[nodiscard]] Result<IdentityRotation> sign_rotation(IdentityRotation r, const crypto::SecretKey& old_secret,
                                                     const crypto::SecretKey& new_secret);
[[nodiscard]] bool verify_rotation(const IdentityRotation& r);

}  // namespace phantomx::poi
