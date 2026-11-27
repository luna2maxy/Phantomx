#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "phantomx/common/error.hpp"

namespace phantomx {

class BinaryWriter {
 public:
  void write_u8(std::uint8_t value);
  void write_u32(std::uint32_t value);
  void write_u64(std::uint64_t value);
  void write_bytes(std::span<const std::uint8_t> bytes);
  void write_string(std::string_view value);

  [[nodiscard]] const std::vector<std::uint8_t>& buffer() const noexcept { return buffer_; }
  [[nodiscard]] std::vector<std::uint8_t> take() noexcept { return std::move(buffer_); }

 private:
  std::vector<std::uint8_t> buffer_;
};

class BinaryReader {
 public:
  explicit BinaryReader(std::span<const std::uint8_t> data) : data_(data) {}

  [[nodiscard]] Result<std::uint8_t> read_u8();
  [[nodiscard]] Result<std::uint32_t> read_u32();
  [[nodiscard]] Result<std::uint64_t> read_u64();
  [[nodiscard]] Result<std::vector<std::uint8_t>> read_bytes(std::size_t len);
  [[nodiscard]] Result<std::string> read_string();

  [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - offset_; }
  [[nodiscard]] bool eof() const noexcept { return offset_ >= data_.size(); }

 private:
  std::span<const std::uint8_t> data_;
  std::size_t offset_ = 0;
};

class Serializable {
 public:
  virtual ~Serializable() = default;
  virtual void serialize(BinaryWriter& writer) const = 0;
  virtual Result<void> deserialize(BinaryReader& reader) = 0;
};

}  // namespace phantomx
