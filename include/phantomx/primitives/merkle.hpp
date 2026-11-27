#pragma once

#include <vector>

#include "phantomx/primitives/hash.hpp"

namespace phantomx {

[[nodiscard]] Hash256Bytes merkle_root(std::vector<Hash256Bytes> hashes);

}  // namespace phantomx
