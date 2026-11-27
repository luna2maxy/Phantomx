#include "phantomx/common/crypto.hpp"

#include <sodium.h>

#include <mutex>

#include "phantomx/primitives/hash.hpp"

namespace phantomx::crypto {
namespace {
std::once_flag g_sodium_once;
bool g_sodium_ok = false;
}  // namespace

std::string_view to_string(KeyRole role) noexcept {
  switch (role) {
    case KeyRole::kWalletSigning: return "wallet_signing";
    case KeyRole::kNodeIdentity: return "node_identity";
    case KeyRole::kVerifierIdentity: return "verifier_identity";
  }
  return "unknown";
}

Result<void> init() {
  std::call_once(g_sodium_once, []() { g_sodium_ok = sodium_init() >= 0; });
  if (!g_sodium_ok) return MakeError(ErrorCode::kInternalError, "libsodium initialization failed");
  return Result<void>{};
}

Result<void> random_bytes(std::span<std::uint8_t> out) {
  auto ok = init();
  if (!ok.ok()) return ok.error();
  if (out.empty()) return Result<void>{};
  randombytes_buf(out.data(), out.size());
  return Result<void>{};
}

Result<KeyPair> generate_keypair(KeyRole role) {
  auto ok = init();
  if (!ok.ok()) return ok.error();
  KeyPair kp;
  kp.role = role;
  if (crypto_sign_keypair(kp.pub.bytes.data(), kp.sec.bytes.data()) != 0) {
    return MakeError(ErrorCode::kInternalError, "keypair generation failed");
  }
  return kp;
}

Result<KeyPair> keypair_from_seed(KeyRole role, std::span<const std::uint8_t, kSeedBytes> seed) {
  auto ok = init();
  if (!ok.ok()) return ok.error();
  KeyPair kp;
  kp.role = role;
  if (crypto_sign_seed_keypair(kp.pub.bytes.data(), kp.sec.bytes.data(), seed.data()) != 0) {
    return MakeError(ErrorCode::kInternalError, "keypair from seed failed");
  }
  return kp;
}

Result<Signature> sign(std::span<const std::uint8_t> message, const SecretKey& secret_key) {
  auto ok = init();
  if (!ok.ok()) return ok.error();
  Signature sig{};
  unsigned long long siglen = 0;
  if (crypto_sign_detached(sig.bytes.data(), &siglen, message.data(), static_cast<unsigned long long>(message.size()),
                           secret_key.bytes.data()) != 0 ||
      siglen != kSignatureBytes) {
    return MakeError(ErrorCode::kInternalError, "signing failed");
  }
  return sig;
}

bool verify(std::span<const std::uint8_t> message, const Signature& signature, const PublicKey& public_key) {
  auto ok = init();
  if (!ok.ok()) return false;
  return crypto_sign_verify_detached(signature.bytes.data(), message.data(),
                                     static_cast<unsigned long long>(message.size()), public_key.bytes.data()) == 0;
}

Result<std::array<std::uint8_t, kMacBytes>> mac(std::span<const std::uint8_t> message,
                                                std::span<const std::uint8_t, 32> key) {
  auto ok = init();
  if (!ok.ok()) return ok.error();
  std::array<std::uint8_t, kMacBytes> out{};
  if (crypto_auth(out.data(), message.data(), static_cast<unsigned long long>(message.size()), key.data()) != 0) {
    return MakeError(ErrorCode::kInternalError, "mac generation failed");
  }
  return out;
}

bool verify_mac(std::span<const std::uint8_t> message, std::span<const std::uint8_t, kMacBytes> tag,
                std::span<const std::uint8_t, 32> key) {
  auto ok = init();
  if (!ok.ok()) return false;
  return crypto_auth_verify(tag.data(), message.data(), static_cast<unsigned long long>(message.size()), key.data()) == 0;
}

std::string serialize_public_key_hex(const PublicKey& key) {
  return to_hex(std::span<const std::uint8_t>(key.bytes.data(), key.bytes.size()));
}

Result<PublicKey> parse_public_key_hex(std::string_view hex) {
  std::vector<std::uint8_t> bytes;
  if (!from_hex(hex, bytes) || bytes.size() != kPublicKeyBytes) {
    return MakeError(ErrorCode::kParseError, "invalid public key hex");
  }
  PublicKey key{};
  std::copy(bytes.begin(), bytes.end(), key.bytes.begin());
  return key;
}

std::string serialize_secret_key_hex(const SecretKey& key) {
  return to_hex(std::span<const std::uint8_t>(key.bytes.data(), key.bytes.size()));
}

Result<SecretKey> parse_secret_key_hex(std::string_view hex) {
  std::vector<std::uint8_t> bytes;
  if (!from_hex(hex, bytes) || bytes.size() != kSecretKeyBytes) {
    return MakeError(ErrorCode::kParseError, "invalid secret key hex");
  }
  SecretKey key{};
  std::copy(bytes.begin(), bytes.end(), key.bytes.begin());
  return key;
}

std::string serialize_signature_hex(const Signature& sig) {
  return to_hex(std::span<const std::uint8_t>(sig.bytes.data(), sig.bytes.size()));
}

Result<Signature> parse_signature_hex(std::string_view hex) {
  std::vector<std::uint8_t> bytes;
  if (!from_hex(hex, bytes) || bytes.size() != kSignatureBytes) {
    return MakeError(ErrorCode::kParseError, "invalid signature hex");
  }
  Signature sig{};
  std::copy(bytes.begin(), bytes.end(), sig.bytes.begin());
  return sig;
}

}  // namespace phantomx::crypto
