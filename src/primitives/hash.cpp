#include "phantomx/primitives/hash.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace phantomx {
namespace {

struct Sha256Ctx {
  std::array<std::uint32_t, 8> state{};
  std::array<std::uint8_t, 64> block{};
  std::uint64_t bitcount = 0;
  std::size_t datalen = 0;
};

constexpr std::array<std::uint32_t, 64> k = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n) { return (x >> n) | (x << (32u - n)); }
inline std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (x & y) ^ (~x & z); }
inline std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
inline std::uint32_t ep0(std::uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
inline std::uint32_t ep1(std::uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
inline std::uint32_t sig0(std::uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
inline std::uint32_t sig1(std::uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

void transform(Sha256Ctx& ctx, const std::uint8_t data[64]) {
  std::array<std::uint32_t, 64> m{};
  for (std::size_t i = 0, j = 0; i < 16; ++i, j += 4) {
    m[i] = (static_cast<std::uint32_t>(data[j]) << 24) | (static_cast<std::uint32_t>(data[j + 1]) << 16) |
           (static_cast<std::uint32_t>(data[j + 2]) << 8) | static_cast<std::uint32_t>(data[j + 3]);
  }
  for (std::size_t i = 16; i < 64; ++i) {
    m[i] = sig1(m[i - 2]) + m[i - 7] + sig0(m[i - 15]) + m[i - 16];
  }
  auto a = ctx.state[0], b = ctx.state[1], c = ctx.state[2], d = ctx.state[3];
  auto e = ctx.state[4], f = ctx.state[5], g = ctx.state[6], h = ctx.state[7];
  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t t1 = h + ep1(e) + ch(e, f, g) + k[i] + m[i];
    const std::uint32_t t2 = ep0(a) + maj(a, b, c);
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  ctx.state[0] += a; ctx.state[1] += b; ctx.state[2] += c; ctx.state[3] += d;
  ctx.state[4] += e; ctx.state[5] += f; ctx.state[6] += g; ctx.state[7] += h;
}

void init(Sha256Ctx& ctx) {
  ctx.datalen = 0;
  ctx.bitcount = 0;
  ctx.state = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
}

void update(Sha256Ctx& ctx, std::span<const std::uint8_t> data) {
  for (auto byte : data) {
    ctx.block[ctx.datalen] = byte;
    ++ctx.datalen;
    if (ctx.datalen == 64) {
      transform(ctx, ctx.block.data());
      ctx.bitcount += 512;
      ctx.datalen = 0;
    }
  }
}

Hash256Bytes final(Sha256Ctx& ctx) {
  std::size_t i = ctx.datalen;
  if (ctx.datalen < 56) {
    ctx.block[i++] = 0x80;
    while (i < 56) ctx.block[i++] = 0x00;
  } else {
    ctx.block[i++] = 0x80;
    while (i < 64) ctx.block[i++] = 0x00;
    transform(ctx, ctx.block.data());
    std::fill(ctx.block.begin(), ctx.block.begin() + 56, 0);
  }
  ctx.bitcount += ctx.datalen * 8;
  ctx.block[56] = static_cast<std::uint8_t>(ctx.bitcount >> 56);
  ctx.block[57] = static_cast<std::uint8_t>(ctx.bitcount >> 48);
  ctx.block[58] = static_cast<std::uint8_t>(ctx.bitcount >> 40);
  ctx.block[59] = static_cast<std::uint8_t>(ctx.bitcount >> 32);
  ctx.block[60] = static_cast<std::uint8_t>(ctx.bitcount >> 24);
  ctx.block[61] = static_cast<std::uint8_t>(ctx.bitcount >> 16);
  ctx.block[62] = static_cast<std::uint8_t>(ctx.bitcount >> 8);
  ctx.block[63] = static_cast<std::uint8_t>(ctx.bitcount);
  transform(ctx, ctx.block.data());

  Hash256Bytes hash{};
  for (i = 0; i < 4; ++i) {
    hash[i] = static_cast<std::uint8_t>((ctx.state[0] >> (24 - i * 8)) & 0xffu);
    hash[i + 4] = static_cast<std::uint8_t>((ctx.state[1] >> (24 - i * 8)) & 0xffu);
    hash[i + 8] = static_cast<std::uint8_t>((ctx.state[2] >> (24 - i * 8)) & 0xffu);
    hash[i + 12] = static_cast<std::uint8_t>((ctx.state[3] >> (24 - i * 8)) & 0xffu);
    hash[i + 16] = static_cast<std::uint8_t>((ctx.state[4] >> (24 - i * 8)) & 0xffu);
    hash[i + 20] = static_cast<std::uint8_t>((ctx.state[5] >> (24 - i * 8)) & 0xffu);
    hash[i + 24] = static_cast<std::uint8_t>((ctx.state[6] >> (24 - i * 8)) & 0xffu);
    hash[i + 28] = static_cast<std::uint8_t>((ctx.state[7] >> (24 - i * 8)) & 0xffu);
  }
  return hash;
}

}  // namespace

Hash256Bytes sha256(std::span<const std::uint8_t> data) {
  Sha256Ctx ctx;
  init(ctx);
  update(ctx, data);
  return final(ctx);
}

Hash256Bytes sha256d(std::span<const std::uint8_t> data) {
  const auto first = sha256(data);
  return sha256(std::span<const std::uint8_t>(first.data(), first.size()));
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (auto b : bytes) oss << std::setw(2) << static_cast<int>(b);
  return oss.str();
}

std::string to_hex_reversed(std::span<const std::uint8_t> bytes) {
  std::vector<std::uint8_t> rev(bytes.rbegin(), bytes.rend());
  return to_hex(rev);
}

bool from_hex(std::string_view hex, std::vector<std::uint8_t>& out) {
  if (hex.size() % 2 != 0) return false;
  out.clear();
  out.reserve(hex.size() / 2);
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
  };
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    const int hi = nibble(hex[i]);
    const int lo = nibble(hex[i + 1]);
    if (hi < 0 || lo < 0) return false;
    out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
  }
  return true;
}

}  // namespace phantomx
