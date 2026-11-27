#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "phantomx/common/error.hpp"
#include "phantomx/primitives/hash.hpp"
#include "phantomx/primitives/transaction.hpp"

namespace phantomx {

inline constexpr std::uint32_t kBlockVersionV1 = 1;

struct BlockHeader {
  std::uint32_t version = kBlockVersionV1;
  Hash256Bytes prev_hash{};
  Hash256Bytes merkle_root{};
  std::uint64_t timestamp = 0;
  std::uint32_t height = 0;
  std::uint32_t nonce = 0;
  std::uint32_t bits = 0;  // compact difficulty/target placeholder for PoI+BFT era; consensus rules define meaning per version
};

struct Block {
  BlockHeader header;
  std::vector<Transaction> transactions;
};

[[nodiscard]] std::vector<std::uint8_t> serialize_block_header(const BlockHeader& header);
[[nodiscard]] Result<BlockHeader> deserialize_block_header(std::span<const std::uint8_t> bytes);
[[nodiscard]] Hash256Bytes block_hash(const BlockHeader& header);
[[nodiscard]] std::vector<std::uint8_t> serialize_block(const Block& block);
[[nodiscard]] Result<Block> deserialize_block(std::span<const std::uint8_t> bytes);

}  // namespace phantomx
