#include "phantomx/consensus/pow.hpp"
#include <algorithm>

namespace phantomx::consensus {

std::array<std::uint8_t, 32> compact_to_target(std::uint32_t compact) {
  std::array<std::uint8_t, 32> target{};
  const std::uint32_t mantissa = compact & 0x007fffff;
  const std::uint32_t exponent = compact >> 24;
  if (mantissa == 0) return target;
  // target = mantissa * 256^(exponent-3)
  std::array<std::uint8_t, 32> tmp{};
  tmp[29] = static_cast<std::uint8_t>((mantissa >> 16) & 0xff);
  tmp[30] = static_cast<std::uint8_t>((mantissa >> 8) & 0xff);
  tmp[31] = static_cast<std::uint8_t>(mantissa & 0xff);
  if (exponent <= 3) {
    const std::uint32_t shift = 8 * (3 - exponent);
    // shift right by shift bits
    std::uint32_t carry = 0;
    for (std::size_t i = 0; i < tmp.size(); ++i) {
      const std::uint32_t cur = (carry << 8) | tmp[i];
      tmp[i] = static_cast<std::uint8_t>(cur >> shift);
      carry = cur & ((1u << shift) - 1u);
    }
    return tmp;
  }
  const std::uint32_t shift_bytes = exponent - 3;
  if (shift_bytes > 32) return target; // overflow -> zero target (impossible)
  // shift left by shift_bytes bytes
  for (std::size_t i = 0; i < target.size(); ++i) {
    const std::size_t src = i + static_cast<std::size_t>(shift_bytes);
    if (src < tmp.size()) target[i] = tmp[src];
  }
  return target;
}

std::uint32_t target_to_compact(const std::array<std::uint8_t, 32>& target) {
  std::size_t first = 0;
  while (first < target.size() && target[first] == 0) ++first;
  if (first == target.size()) return 0;
  std::uint32_t exponent = static_cast<std::uint32_t>(target.size() - first);
  std::uint32_t mantissa = 0;
  if (exponent <= 3) {
    mantissa = static_cast<std::uint32_t>(target[31]);
    if (exponent >= 2) mantissa |= static_cast<std::uint32_t>(target[30]) << 8;
    if (exponent >= 3) mantissa |= static_cast<std::uint32_t>(target[29]) << 16;
    mantissa >>= 8 * (3 - exponent);
  } else {
    mantissa = (static_cast<std::uint32_t>(target[first]) << 16) |
               (static_cast<std::uint32_t>(first + 1 < target.size() ? target[first + 1] : 0) << 8) |
               static_cast<std::uint32_t>(first + 2 < target.size() ? target[first + 2] : 0);
  }
  if (mantissa & 0x00800000u) { mantissa >>= 8; ++exponent; }
  return (exponent << 24) | (mantissa & 0x007fffff);
}

bool hash_meets_target(const Hash256Bytes& hash, std::uint32_t compact_bits) {
  if (compact_bits == 0) return false;
  const auto target = compact_to_target(compact_bits);
  // hash is big-endian 256-bit; compare as big-endian
  for (size_t i = 0; i < 32; ++i) {
    if (hash[i] < target[i]) return true;
    if (hash[i] > target[i]) return false;
  }
  return true;
}

bool check_proof_of_work(const Hash256Bytes& hash, std::uint32_t compact_bits) {
  return hash_meets_target(hash, compact_bits);
}

} // namespace phantomx::consensus
