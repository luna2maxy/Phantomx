#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace phantomx {

using PhxAtom = std::uint64_t;

inline constexpr PhxAtom kPhxAtomsPerPhx = 100'000'000ULL;
inline constexpr std::uint8_t kPhxDecimals = 8;
inline constexpr std::uint64_t kInitialBlockRewardAtoms = 2'500'000'000ULL; // 25 PHX
inline constexpr std::uint64_t kHalvingIntervalBlocks = 2'100'000ULL;
inline constexpr std::uint64_t kTargetBlockTimeSeconds = 60ULL;
inline constexpr std::uint64_t kTheoreticalMaxSupplyAtoms = 10'500'000'000'000'000ULL; // 105,000,000 PHX
inline constexpr std::string_view kAddressPrefix = "PhantomX";

struct Hash256 {
  std::array<std::uint8_t, 32> bytes{};

  [[nodiscard]] bool operator==(const Hash256& other) const noexcept = default;
};

struct Version {
  int major = 0;
  int minor = 0;
  int patch = 0;

  [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] std::string atoms_to_phx_string(PhxAtom atoms);
[[nodiscard]] bool parse_phx_atoms(std::string_view text, PhxAtom& out_atoms);

}  // namespace phantomx
