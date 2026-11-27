#pragma once
#include <cstdint>
#include <array>
#include "phantomx/common/error.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::consensus {

inline constexpr std::uint32_t kRegtestPowBits = 0x207fffff; // easy regtest target

[[nodiscard]] std::array<std::uint8_t, 32> compact_to_target(std::uint32_t compact);
[[nodiscard]] std::uint32_t target_to_compact(const std::array<std::uint8_t, 32>& target);
[[nodiscard]] bool hash_meets_target(const Hash256Bytes& hash, std::uint32_t compact_bits);
[[nodiscard]] bool check_proof_of_work(const Hash256Bytes& hash, std::uint32_t compact_bits);

} // namespace phantomx::consensus
