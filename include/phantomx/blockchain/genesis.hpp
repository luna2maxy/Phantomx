#pragma once

#include <string_view>

#include "phantomx/primitives/block.hpp"

namespace phantomx {

enum class Network { kMainnet, kTestnet, kRegtest };

[[nodiscard]] std::string_view network_name(Network network);
[[nodiscard]] std::uint32_t network_magic(Network network);
[[nodiscard]] std::string_view address_hrp(Network network);
[[nodiscard]] Block genesis_block(Network network);

}  // namespace phantomx
