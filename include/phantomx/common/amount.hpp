#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

#include "phantomx/common/types.hpp"

namespace phantomx {

// Amount in smallest PHX unit (phx_atom). Consensus/economic arithmetic must use integers only.
using AmountAtoms = PhxAtom;

inline constexpr AmountAtoms kCoin = kPhxAtomsPerPhx;  // 1 PHX = 100,000,000 atoms
inline constexpr AmountAtoms kCent = kCoin / 100;

// Fixed-point basis points: 10000 = 100%.
using BasisPoints = std::uint32_t;
inline constexpr BasisPoints kBpsDenom = 10'000;

// Fixed-point milli score: 100000 = 100.000% (used by PoI).
using ScoreMilli = std::uint32_t;
inline constexpr ScoreMilli kScoreScale = 100'000;

[[nodiscard]] constexpr bool amount_add_overflows(AmountAtoms a, AmountAtoms b) noexcept {
  return b > (std::numeric_limits<AmountAtoms>::max() - a);
}

[[nodiscard]] constexpr bool amount_mul_overflows(AmountAtoms a, AmountAtoms b) noexcept {
  if (a == 0 || b == 0) return false;
  return a > (std::numeric_limits<AmountAtoms>::max() / b);
}

[[nodiscard]] constexpr AmountAtoms checked_add_sat(AmountAtoms a, AmountAtoms b) noexcept {
  return amount_add_overflows(a, b) ? std::numeric_limits<AmountAtoms>::max() : (a + b);
}

}  // namespace phantomx
