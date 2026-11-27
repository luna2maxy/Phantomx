#include "phantomx/common/serialization.hpp"

#include <cstring>

namespace phantomx {

namespace {
template <typename T>
void write_le(std::vector<std::uint8_t>& out, T value) {
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    out.push_back(static_cast<std::uint8_t>((static_cast<std::uint64_t>(value) >> (i * 8)) & 0xFFu));
  }
}
}  // namespace

void BinaryWriter::write_u8(std::uint8_t value) { buffer_.push_back(value); }
void BinaryWriter::write_u32(std::uint32_t value) { write_le(buffer_, value); }
void BinaryWriter::write_u64(std::uint64_t value) { write_le(buffer_, value); }

void BinaryWriter::write_bytes(std::span<const std::uint8_t> bytes) {
  write_u64(static_cast<std::uint64_t>(bytes.size()));
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

void BinaryWriter::write_string(std::string_view value) {
  write_u64(static_cast<std::uint64_t>(value.size()));
  buffer_.insert(buffer_.end(), value.begin(), value.end());
}

Result<std::uint8_t> BinaryReader::read_u8() {
  if (remaining() < 1) return MakeError(ErrorCode::kSerializationError, "unexpected end of buffer reading u8");
  return data_[offset_++];
}

Result<std::uint32_t> BinaryReader::read_u32() {
  if (remaining() < 4) return MakeError(ErrorCode::kSerializationError, "unexpected end of buffer reading u32");
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(data_[offset_ + i]) << (i * 8);
  }
  offset_ += 4;
  return value;
}

Result<std::uint64_t> BinaryReader::read_u64() {
  if (remaining() < 8) return MakeError(ErrorCode::kSerializationError, "unexpected end of buffer reading u64");
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(data_[offset_ + i]) << (i * 8);
  }
  offset_ += 8;
  return value;
}

Result<std::vector<std::uint8_t>> BinaryReader::read_bytes(std::size_t len) {
  if (remaining() < len) return MakeError(ErrorCode::kSerializationError, "unexpected end of buffer reading bytes");
  std::vector<std::uint8_t> out(data_.begin() + static_cast<std::ptrdiff_t>(offset_),
                                data_.begin() + static_cast<std::ptrdiff_t>(offset_ + len));
  offset_ += len;
  return out;
}

Result<std::string> BinaryReader::read_string() {
  auto len_result = read_u64();
  if (!len_result.ok()) return len_result.error();
  const auto len = static_cast<std::size_t>(len_result.value());
  if (remaining() < len) return MakeError(ErrorCode::kSerializationError, "unexpected end of buffer reading string");
  std::string out(reinterpret_cast<const char*>(data_.data() + offset_), len);
  offset_ += len;
  return out;
}

}  // namespace phantomx
