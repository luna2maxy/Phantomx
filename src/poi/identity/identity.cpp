#include "phantomx/poi/identity/identity.hpp"

#include <string>

#include "phantomx/common/serialization.hpp"

namespace phantomx::poi {
namespace {
std::vector<std::uint8_t> identity_preimage(const NodeIdentity& id) {
  BinaryWriter w;
  w.write_u64(id.version);
  w.write_bytes(std::span<const std::uint8_t>(id.node_pubkey.data(), id.node_pubkey.size()));
  w.write_bytes(std::span<const std::uint8_t>(id.operator_pubkey.data(), id.operator_pubkey.size()));
  w.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(id.wallet_address.data()), id.wallet_address.size()));
  w.write_bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(id.network_endpoint.data()), id.network_endpoint.size()));
  w.write_u64(id.registered_at);
  w.write_u64(id.expires_at);
  w.write_bytes(std::span<const std::uint8_t>(id.registration_nonce.data(), id.registration_nonce.size()));
  return w.take();
}
}  // namespace

std::vector<std::uint8_t> serialize_identity(const NodeIdentity& id) {
  auto pre = identity_preimage(id);
  BinaryWriter w;
  w.write_bytes(pre);
  w.write_bytes(std::span<const std::uint8_t>(id.signature.data(), id.signature.size()));
  return w.take();
}

Hash256Bytes identity_hash(const NodeIdentity& id) {
  auto pre = identity_preimage(id);
  const std::string domain = "PHX/poi/identity/v1";
  std::vector<std::uint8_t> msg(domain.begin(), domain.end());
  msg.insert(msg.end(), pre.begin(), pre.end());
  return sha256d(msg);
}

Result<NodeIdentity> sign_identity(NodeIdentity id, const crypto::SecretKey& node_secret) {
  const auto h = identity_hash(id);
  auto sig = crypto::sign(std::span<const std::uint8_t>(h.data(), h.size()), node_secret);
  if (!sig.ok()) return sig.error();
  id.signature = sig.value().bytes;
  return id;
}

bool verify_identity(const NodeIdentity& id) {
  const auto h = identity_hash(id);
  crypto::PublicKey pk{}; pk.bytes = id.node_pubkey;
  crypto::Signature sig{}; sig.bytes = id.signature;
  return crypto::verify(std::span<const std::uint8_t>(h.data(), h.size()), sig, pk);
}

bool identity_fresh(const NodeIdentity& id, std::uint64_t now, std::uint64_t max_age_seconds) {
  if (id.registered_at > now) return false;
  if (id.expires_at != 0 && now > id.expires_at) return false;
  return (now - id.registered_at) <= max_age_seconds;
}

std::vector<std::uint8_t> serialize_rotation(const IdentityRotation& r) {
  BinaryWriter w;
  w.write_bytes(std::span<const std::uint8_t>(r.old_node_pubkey.data(), r.old_node_pubkey.size()));
  w.write_bytes(std::span<const std::uint8_t>(r.new_node_pubkey.data(), r.new_node_pubkey.size()));
  w.write_u64(r.rotated_at);
  w.write_bytes(std::span<const std::uint8_t>(r.nonce.data(), r.nonce.size()));
  w.write_bytes(std::span<const std::uint8_t>(r.signature_by_old.data(), r.signature_by_old.size()));
  w.write_bytes(std::span<const std::uint8_t>(r.signature_by_new.data(), r.signature_by_new.size()));
  return w.take();
}

Hash256Bytes rotation_hash(const IdentityRotation& r) {
  BinaryWriter w;
  w.write_bytes(std::span<const std::uint8_t>(r.old_node_pubkey.data(), r.old_node_pubkey.size()));
  w.write_bytes(std::span<const std::uint8_t>(r.new_node_pubkey.data(), r.new_node_pubkey.size()));
  w.write_u64(r.rotated_at);
  w.write_bytes(std::span<const std::uint8_t>(r.nonce.data(), r.nonce.size()));
  auto bytes = w.take();
  const std::string domain = "PHX/poi/identity-rotation/v1";
  std::vector<std::uint8_t> msg(domain.begin(), domain.end());
  msg.insert(msg.end(), bytes.begin(), bytes.end());
  return sha256d(msg);
}

Result<IdentityRotation> sign_rotation(IdentityRotation r, const crypto::SecretKey& old_secret,
                                       const crypto::SecretKey& new_secret) {
  const auto h = rotation_hash(r);
  auto s1 = crypto::sign(std::span<const std::uint8_t>(h.data(), h.size()), old_secret);
  if (!s1.ok()) return s1.error();
  auto s2 = crypto::sign(std::span<const std::uint8_t>(h.data(), h.size()), new_secret);
  if (!s2.ok()) return s2.error();
  r.signature_by_old = s1.value().bytes;
  r.signature_by_new = s2.value().bytes;
  return r;
}

bool verify_rotation(const IdentityRotation& r) {
  const auto h = rotation_hash(r);
  crypto::PublicKey old_pk{}; old_pk.bytes = r.old_node_pubkey;
  crypto::PublicKey new_pk{}; new_pk.bytes = r.new_node_pubkey;
  crypto::Signature s1{}; s1.bytes = r.signature_by_old;
  crypto::Signature s2{}; s2.bytes = r.signature_by_new;
  const auto msg = std::span<const std::uint8_t>(h.data(), h.size());
  return crypto::verify(msg, s1, old_pk) && crypto::verify(msg, s2, new_pk);
}

}  // namespace phantomx::poi
