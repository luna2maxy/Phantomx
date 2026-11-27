#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "phantomx/common/config.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/common/error.hpp"
#include "phantomx/consensus/types.hpp"

namespace phantomx::consensus {

enum class BftKeyMode : std::uint8_t {
  kSeed = 1,      // deterministic seed prefix (regtest/dev only, explicit config)
  kOperator = 2,  // operator-managed signing key file; pubkeys in config
};

struct BftValidatorConfig {
  std::array<std::uint8_t, 32> pubkey{};
  std::uint64_t voting_power = 1;
};

struct BftConfig {
  bool enabled = false;
  BftKeyMode key_mode = BftKeyMode::kSeed;
  std::uint32_t validator_count = 0;
  bool view_change_enabled = false;
  std::string validator_seed_prefix;
  std::vector<BftValidatorConfig> validators;
  std::string signing_key_file;  // path; secret key loaded separately
};

struct LoadedBftIdentity {
  crypto::KeyPair local_key;
  std::size_t validator_index = 0;
  std::array<std::uint8_t, 32> validator_pubkey{};
};

// Parses and validates BFT config from flat Config keys. Does not load secret key material.
// Canonical/production-path validation: rejects ambiguous/missing/invalid fields fail-closed.
[[nodiscard]] Result<BftConfig> parse_bft_config(const Config& cfg);

// Builds ValidatorSet from parsed BFT config. For seed mode, derives deterministic keys from prefix.
// For operator mode, uses configured public keys only.
[[nodiscard]] Result<ValidatorSet> build_validator_set(const BftConfig& cfg, std::uint64_t epoch = 0);

// Loads operator signing key from file and verifies it matches a configured validator.
// Fails closed on missing/invalid/mismatch/duplicate identity.
[[nodiscard]] Result<LoadedBftIdentity> load_operator_identity(const BftConfig& cfg,
                                                               const std::filesystem::path& signing_key_file);

// Resolves local validator index for seed mode from --node-index (1-based).
[[nodiscard]] Result<std::size_t> resolve_seed_node_index(std::uint32_t node_index_1based,
                                                          std::uint32_t validator_count);

// Loads a libsodium crypto_sign secret key from a hex file. File must contain 128 hex chars.
// Never logs or returns key material in error messages.
[[nodiscard]] Result<crypto::SecretKey> load_secret_key_hex_file(const std::filesystem::path& path);

// Validates consensus config consistency for startup. Returns error with actionable message (no secrets).
[[nodiscard]] Result<void> validate_bft_startup(const BftConfig& cfg, const Config& raw_cfg);

}  // namespace phantomx::consensus
