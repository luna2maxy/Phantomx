#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phantomx/common/error.hpp"

namespace phantomx {

[[nodiscard]] Result<std::string> bech32m_encode(std::string_view hrp, std::span<const std::uint8_t> data5);
[[nodiscard]] Result<std::pair<std::string, std::vector<std::uint8_t>>> bech32m_decode(std::string_view addr);
[[nodiscard]] std::vector<std::uint8_t> convert_bits(std::span<const std::uint8_t> in, int from_bits, int to_bits, bool pad);
[[nodiscard]] Result<std::string> encode_phantomx_address(std::string_view hrp, std::uint8_t version, std::span<const std::uint8_t> payload);
[[nodiscard]] Result<std::pair<std::uint8_t, std::vector<std::uint8_t>>> decode_phantomx_address(std::string_view hrp_expected, std::string_view addr);

}  // namespace phantomx
