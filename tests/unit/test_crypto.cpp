#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/primitives/hash.hpp"
#include "phantomx/wallet/keystore.hpp"

namespace {
std::vector<std::uint8_t> hex(const std::string& h) {
  std::vector<std::uint8_t> out;
  PHX_EXPECT(phantomx::from_hex(h, out));
  return out;
}
}  // namespace

PHX_TEST(crypto_sign_verify_valid_vector) {
  auto init = phantomx::crypto::init();
  PHX_EXPECT(init.ok());
  auto seed_vec = hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
  std::array<std::uint8_t, phantomx::crypto::kSeedBytes> seed{};
  std::copy(seed_vec.begin(), seed_vec.end(), seed.begin());
  auto kp = phantomx::crypto::keypair_from_seed(phantomx::crypto::KeyRole::kWalletSigning,
                                                std::span<const std::uint8_t, phantomx::crypto::kSeedBytes>(seed));
  PHX_EXPECT(kp.ok());
  PHX_EXPECT(phantomx::crypto::serialize_public_key_hex(kp.value().pub) ==
             "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8");
  auto msg = hex("5068616e746f6d582074657374206d657373616765");
  auto sig = phantomx::crypto::sign(msg, kp.value().sec);
  PHX_EXPECT(sig.ok());
  PHX_EXPECT(phantomx::crypto::serialize_signature_hex(sig.value()) ==
             "726485298113f9a367f332bb11ce1fea1bd30483dc9318a3bd85a47e1f3a059509e7b29af536bc2b42ac8940b922be1e7e4b76ba5ca9477a4734bc5f3a2ddc00");
  PHX_EXPECT(phantomx::crypto::verify(msg, sig.value(), kp.value().pub));
}

PHX_TEST(crypto_rejects_tampered_message) {
  auto seed_vec = hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
  std::array<std::uint8_t, phantomx::crypto::kSeedBytes> seed{};
  std::copy(seed_vec.begin(), seed_vec.end(), seed.begin());
  auto kp = phantomx::crypto::keypair_from_seed(phantomx::crypto::KeyRole::kWalletSigning,
                                                std::span<const std::uint8_t, phantomx::crypto::kSeedBytes>(seed));
  PHX_EXPECT(kp.ok());
  auto msg = hex("5068616e746f6d582074657374206d657373616765");
  auto sig = phantomx::crypto::sign(msg, kp.value().sec);
  PHX_EXPECT(sig.ok());
  msg.back() ^= 0x01;
  PHX_EXPECT(!phantomx::crypto::verify(msg, sig.value(), kp.value().pub));
}

PHX_TEST(crypto_rejects_wrong_key) {
  auto seed1 = hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
  auto seed2 = std::vector<std::uint8_t>(32, 0x01);
  std::array<std::uint8_t, 32> s1{}, s2{};
  std::copy(seed1.begin(), seed1.end(), s1.begin());
  std::copy(seed2.begin(), seed2.end(), s2.begin());
  auto kp1 = phantomx::crypto::keypair_from_seed(phantomx::crypto::KeyRole::kWalletSigning, std::span<const std::uint8_t,32>(s1));
  auto kp2 = phantomx::crypto::keypair_from_seed(phantomx::crypto::KeyRole::kNodeIdentity, std::span<const std::uint8_t,32>(s2));
  PHX_EXPECT(kp1.ok() && kp2.ok());
  auto msg = hex("5068616e746f6d582074657374206d657373616765");
  auto sig = phantomx::crypto::sign(msg, kp1.value().sec);
  PHX_EXPECT(sig.ok());
  PHX_EXPECT(!phantomx::crypto::verify(msg, sig.value(), kp2.value().pub));
}

PHX_TEST(crypto_rejects_invalid_signature) {
  auto seed_vec = hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
  std::array<std::uint8_t, 32> seed{};
  std::copy(seed_vec.begin(), seed_vec.end(), seed.begin());
  auto kp = phantomx::crypto::keypair_from_seed(phantomx::crypto::KeyRole::kWalletSigning, std::span<const std::uint8_t,32>(seed));
  PHX_EXPECT(kp.ok());
  auto msg = hex("5068616e746f6d582074657374206d657373616765");
  auto sig = phantomx::crypto::sign(msg, kp.value().sec);
  PHX_EXPECT(sig.ok());
  sig.value().bytes[0] ^= 0x01;
  PHX_EXPECT(!phantomx::crypto::verify(msg, sig.value(), kp.value().pub));
}

PHX_TEST(crypto_key_serialization_roundtrip) {
  auto seed_vec = hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
  std::array<std::uint8_t, 32> seed{};
  std::copy(seed_vec.begin(), seed_vec.end(), seed.begin());
  auto kp = phantomx::crypto::keypair_from_seed(phantomx::crypto::KeyRole::kVerifierIdentity, std::span<const std::uint8_t,32>(seed));
  PHX_EXPECT(kp.ok());
  auto pub_hex = phantomx::crypto::serialize_public_key_hex(kp.value().pub);
  auto sec_hex = phantomx::crypto::serialize_secret_key_hex(kp.value().sec);
  auto pub2 = phantomx::crypto::parse_public_key_hex(pub_hex);
  auto sec2 = phantomx::crypto::parse_secret_key_hex(sec_hex);
  PHX_EXPECT(pub2.ok() && sec2.ok());
  PHX_EXPECT(phantomx::crypto::serialize_public_key_hex(pub2.value()) == pub_hex);
  PHX_EXPECT(phantomx::crypto::serialize_secret_key_hex(sec2.value()) == sec_hex);
}

PHX_TEST(crypto_random_bytes_basic) {
  std::array<std::uint8_t, 32> a{}, b{};
  auto r1 = phantomx::crypto::random_bytes(a);
  auto r2 = phantomx::crypto::random_bytes(b);
  PHX_EXPECT(r1.ok() && r2.ok());
  // Extremely unlikely to be equal; this is a basic sanity check, not a randomness proof.
  PHX_EXPECT(a != b);
}

PHX_TEST(keystore_generate_save_load) {
  phantomx::wallet::Keystore ks;
  PHX_EXPECT(ks.generate(phantomx::crypto::KeyRole::kWalletSigning).ok());
  PHX_EXPECT(ks.generate(phantomx::crypto::KeyRole::kNodeIdentity).ok());
  PHX_EXPECT(ks.has(phantomx::crypto::KeyRole::kWalletSigning));
  PHX_EXPECT(ks.has(phantomx::crypto::KeyRole::kNodeIdentity));
  PHX_EXPECT(!ks.has(phantomx::crypto::KeyRole::kVerifierIdentity));
  const auto path = std::filesystem::temp_directory_path() / "phantomx-keystore-test.keys";
  auto saved = ks.save(path);
  PHX_EXPECT(saved.ok());
  auto loaded = phantomx::wallet::Keystore::load(path);
  PHX_EXPECT(loaded.ok());
  auto k1 = ks.get(phantomx::crypto::KeyRole::kWalletSigning);
  auto k2 = loaded.value().get(phantomx::crypto::KeyRole::kWalletSigning);
  PHX_EXPECT(k1.has_value() && k2.has_value());
  PHX_EXPECT(phantomx::crypto::serialize_public_key_hex(k1->pub) == phantomx::crypto::serialize_public_key_hex(k2->pub));
  std::filesystem::remove(path);
}
