#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace phantomx {

using Hash256Bytes = std::array<std::uint8_t, 32>;

[[nodiscard]] Hash256Bytes sha256(std::span<const std::uint8_t> data);
[[nodiscard]] Hash256Bytes sha256d(std::span<const std::uint8_t> data);
[[nodiscard]] std::string to_hex(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::string to_hex_reversed(std::span<const std::uint8_t> bytes);
[[nodiscard]] bool from_hex(std::string_view hex, std::vector<std::uint8_t>& out);

}  // namespace phantomx
