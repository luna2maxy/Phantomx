#include "../test_framework.hpp"
#include "phantomx/common/serialization.hpp"

PHX_TEST(binary_roundtrip_primitives) {
  phantomx::BinaryWriter w;
  w.write_u8(0xAB);
  w.write_u32(0x12345678u);
  w.write_u64(0x1122334455667788ULL);
  w.write_string("phantomx");
  const auto buf = w.buffer();
  phantomx::BinaryReader r(buf);
  auto u8 = r.read_u8();
  auto u32 = r.read_u32();
  auto u64 = r.read_u64();
  auto s = r.read_string();
  PHX_EXPECT(u8.ok() && u8.value() == 0xAB);
  PHX_EXPECT(u32.ok() && u32.value() == 0x12345678u);
  PHX_EXPECT(u64.ok() && u64.value() == 0x1122334455667788ULL);
  PHX_EXPECT(s.ok() && s.value() == "phantomx");
  PHX_EXPECT(r.eof());
}

PHX_TEST(binary_reader_detects_eof) {
  std::vector<std::uint8_t> buf = {1, 2};
  phantomx::BinaryReader r(buf);
  auto v = r.read_u32();
  PHX_EXPECT(!v.ok());
  PHX_EXPECT(v.error().code() == phantomx::ErrorCode::kSerializationError);
}
