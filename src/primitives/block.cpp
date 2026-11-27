#include "phantomx/primitives/block.hpp"

#include "phantomx/common/serialization.hpp"

namespace phantomx {

std::vector<std::uint8_t> serialize_block_header(const BlockHeader& header) {
  BinaryWriter w;
  w.write_u32(header.version);
  w.write_bytes(std::span<const std::uint8_t>(header.prev_hash.data(), header.prev_hash.size()));
  w.write_bytes(std::span<const std::uint8_t>(header.merkle_root.data(), header.merkle_root.size()));
  w.write_u64(header.timestamp);
  w.write_u32(header.height);
  w.write_u32(header.nonce);
  w.write_u32(header.bits);
  return w.take();
}

Result<BlockHeader> deserialize_block_header(std::span<const std::uint8_t> bytes) {
  BinaryReader r(bytes);
  BlockHeader h;
  auto version = r.read_u32(); if (!version.ok()) return version.error(); h.version = version.value();
  auto prev_len = r.read_u64(); if (!prev_len.ok()) return prev_len.error();
  if (prev_len.value() != 32) return MakeError(ErrorCode::kSerializationError, "invalid prev_hash length");
  auto prev = r.read_bytes(32); if (!prev.ok()) return prev.error();
  std::copy(prev.value().begin(), prev.value().end(), h.prev_hash.begin());
  auto merkle_len = r.read_u64(); if (!merkle_len.ok()) return merkle_len.error();
  if (merkle_len.value() != 32) return MakeError(ErrorCode::kSerializationError, "invalid merkle_root length");
  auto merkle = r.read_bytes(32); if (!merkle.ok()) return merkle.error();
  std::copy(merkle.value().begin(), merkle.value().end(), h.merkle_root.begin());
  auto ts = r.read_u64(); if (!ts.ok()) return ts.error(); h.timestamp = ts.value();
  auto height = r.read_u32(); if (!height.ok()) return height.error(); h.height = height.value();
  auto nonce = r.read_u32(); if (!nonce.ok()) return nonce.error(); h.nonce = nonce.value();
  auto bits = r.read_u32(); if (!bits.ok()) return bits.error(); h.bits = bits.value();
  if (!r.eof()) return MakeError(ErrorCode::kSerializationError, "trailing bytes in block header");
  return h;
}

Hash256Bytes block_hash(const BlockHeader& header) {
  const auto bytes = serialize_block_header(header);
  return sha256d(bytes);
}

std::vector<std::uint8_t> serialize_block(const Block& block) {
  BinaryWriter w;
  const auto header_bytes = serialize_block_header(block.header);
  w.write_bytes(header_bytes);
  w.write_u32(static_cast<std::uint32_t>(block.transactions.size()));
  for (const auto& tx : block.transactions) {
    w.write_bytes(serialize_transaction(tx));
  }
  return w.take();
}

Result<Block> deserialize_block(std::span<const std::uint8_t> bytes) {
  BinaryReader r(bytes);
  auto header_len = r.read_u64(); if (!header_len.ok()) return header_len.error();
  auto header_bytes = r.read_bytes(static_cast<std::size_t>(header_len.value())); if (!header_bytes.ok()) return header_bytes.error();
  auto header = deserialize_block_header(header_bytes.value()); if (!header.ok()) return header.error();
  Block b;
  b.header = header.value();
  auto tx_count = r.read_u32(); if (!tx_count.ok()) return tx_count.error();
  b.transactions.resize(tx_count.value());
  for (auto& tx : b.transactions) {
    auto tx_len = r.read_u64(); if (!tx_len.ok()) return tx_len.error();
    auto tx_bytes = r.read_bytes(static_cast<std::size_t>(tx_len.value())); if (!tx_bytes.ok()) return tx_bytes.error();
    auto parsed = deserialize_transaction(tx_bytes.value()); if (!parsed.ok()) return parsed.error();
    tx = std::move(parsed.value());
  }
  if (!r.eof()) return MakeError(ErrorCode::kSerializationError, "trailing bytes in block");
  return b;
}

}  // namespace phantomx
