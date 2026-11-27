#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "phantomx/common/error.hpp"

namespace phantomx::crypto {

inline constexpr std::size_t kSeedBytes = 32;
inline constexpr std::size_t kPublicKeyBytes = 32;
inline constexpr std::size_t kSecretKeyBytes = 64;  // libsodium crypto_sign secret key
inline constexpr std::size_t kSignatureBytes = 64;
inline constexpr std::size_t kMacBytes = 32;

enum class KeyRole : std::uint8_t {
  kWalletSigning = 1,
  kNodeIdentity = 2,
  kVerifierIdentity = 3,
};

[[nodiscard]] std::string_view to_string(KeyRole role) noexcept;

struct PublicKey {
  std::array<std::uint8_t, kPublicKeyBytes> bytes{};
};

struct SecretKey {
  std::array<std::uint8_t, kSecretKeyBytes> bytes{};
};

struct KeyPair {
  KeyRole role = KeyRole::kWalletSigning;
  PublicKey pub{};
  SecretKey sec{};
};

struct Signature {
  std::array<std::uint8_t, kSignatureBytes> bytes{};
};

// Initialization (idempotent). Must be called before crypto operations; called internally by helpers.
[[nodiscard]] Result<void> init();

// Secure random bytes. Uses libsodium randombytes_buf.
[[nodiscard]] Result<void> random_bytes(std::span<std::uint8_t> out);

// Key generation. Uses libsodium crypto_sign_keypair (Ed25519).
[[nodiscard]] Result<KeyPair> generate_keypair(KeyRole role);
[[nodiscard]] Result<KeyPair> keypair_from_seed(KeyRole role, std::span<const std::uint8_t, kSeedBytes> seed);

// Signing/verification. Uses libsodium crypto_sign_detached/verify_detached (Ed25519).
[[nodiscard]] Result<Signature> sign(std::span<const std::uint8_t> message, const SecretKey& secret_key);
[[nodiscard]] bool verify(std::span<const std::uint8_t> message, const Signature& signature, const PublicKey& public_key);

// Message authentication (symmetric). Uses libsodium crypto_auth (HMAC-SHA512-256, key 32 bytes).
[[nodiscard]] Result<std::array<std::uint8_t, kMacBytes>> mac(std::span<const std::uint8_t> message,
                                                             std::span<const std::uint8_t, 32> key);
[[nodiscard]] bool verify_mac(std::span<const std::uint8_t> message,
                              std::span<const std::uint8_t, kMacBytes> tag,
                              std::span<const std::uint8_t, 32> key);

// Key serialization. Private key serialization is explicit; callers must not log returned sensitive material.
[[nodiscard]] std::string serialize_public_key_hex(const PublicKey& key);
[[nodiscard]] Result<PublicKey> parse_public_key_hex(std::string_view hex);
[[nodiscard]] std::string serialize_secret_key_hex(const SecretKey& key);  // SENSITIVE
[[nodiscard]] Result<SecretKey> parse_secret_key_hex(std::string_view hex);  // SENSITIVE

[[nodiscard]] std::string serialize_signature_hex(const Signature& sig);
[[nodiscard]] Result<Signature> parse_signature_hex(std::string_view hex);

}  // namespace phantomx::crypto
