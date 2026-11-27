#include "phantomx/primitives/bech32m.hpp"

#include <algorithm>
#include <cctype>

namespace phantomx {
namespace {
constexpr char kCharset[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
constexpr std::uint32_t kBech32mConst = 0x2bc830a3u;

std::uint32_t polymod(const std::vector<std::uint8_t>& values) {
  std::uint32_t chk = 1;
  for (auto v : values) {
    const std::uint32_t top = chk >> 25;
    chk = ((chk & 0x1ffffffu) << 5) ^ v;
    if (top & 1u) chk ^= 0x3b6a57b2u;
    if (top & 2u) chk ^= 0x26508e6du;
    if (top & 4u) chk ^= 0x1ea119fau;
    if (top & 8u) chk ^= 0x3d4233ddu;
    if (top & 16u) chk ^= 0x2a1462b3u;
  }
  return chk;
}

std::vector<std::uint8_t> hrp_expand(std::string_view hrp) {
  std::vector<std::uint8_t> ret;
  ret.reserve(hrp.size() * 2 + 1);
  for (char c : hrp) ret.push_back(static_cast<std::uint8_t>(c >> 5));
  ret.push_back(0);
  for (char c : hrp) ret.push_back(static_cast<std::uint8_t>(c & 31));
  return ret;
}

bool verify_checksum(std::string_view hrp, const std::vector<std::uint8_t>& data) {
  auto values = hrp_expand(hrp);
  values.insert(values.end(), data.begin(), data.end());
  return polymod(values) == kBech32mConst;
}

std::vector<std::uint8_t> create_checksum(std::string_view hrp, const std::vector<std::uint8_t>& data) {
  auto values = hrp_expand(hrp);
  values.insert(values.end(), data.begin(), data.end());
  values.insert(values.end(), 6, 0);
  const std::uint32_t pm = polymod(values) ^ kBech32mConst;
  std::vector<std::uint8_t> checksum(6);
  for (std::size_t i = 0; i < 6; ++i) checksum[i] = static_cast<std::uint8_t>((pm >> (5 * (5 - i))) & 31u);
  return checksum;
}

std::string to_lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}
}  // namespace

std::vector<std::uint8_t> convert_bits(std::span<const std::uint8_t> in, int from_bits, int to_bits, bool pad) {
  std::uint32_t acc = 0;
  int bits = 0;
  const std::uint32_t maxv = (1u << to_bits) - 1u;
  std::vector<std::uint8_t> ret;
  for (auto value : in) {
    if (value >> from_bits) return {};
    acc = (acc << from_bits) | value;
    bits += from_bits;
    while (bits >= to_bits) {
      bits -= to_bits;
      ret.push_back(static_cast<std::uint8_t>((acc >> bits) & maxv));
    }
  }
  if (pad) {
    if (bits > 0) ret.push_back(static_cast<std::uint8_t>((acc << (to_bits - bits)) & maxv));
  } else if (bits >= from_bits || ((acc << (to_bits - bits)) & maxv) != 0) {
    return {};
  }
  return ret;
}

Result<std::string> bech32m_encode(std::string_view hrp_in, std::span<const std::uint8_t> data5) {
  std::string hrp = to_lower(hrp_in);
  if (hrp.empty() || hrp.size() > 83) return MakeError(ErrorCode::kInvalidArgument, "invalid hrp length");
  for (char c : hrp) {
    if (static_cast<unsigned char>(c) < 33 || static_cast<unsigned char>(c) > 126) {
      return MakeError(ErrorCode::kInvalidArgument, "invalid hrp character");
    }
  }
  for (auto v : data5) if (v > 31) return MakeError(ErrorCode::kInvalidArgument, "invalid data value");
  std::vector<std::uint8_t> data(data5.begin(), data5.end());
  const auto checksum = create_checksum(hrp, data);
  data.insert(data.end(), checksum.begin(), checksum.end());
  std::string out = hrp + "1";
  for (auto v : data) out.push_back(kCharset[v]);
  return out;
}

Result<std::pair<std::string, std::vector<std::uint8_t>>> bech32m_decode(std::string_view addr) {
  bool has_lower = false, has_upper = false;
  for (char c : addr) {
    if (c < 33 || c > 126) return MakeError(ErrorCode::kInvalidArgument, "invalid address character");
    if (c >= 'a' && c <= 'z') has_lower = true;
    if (c >= 'A' && c <= 'Z') has_upper = true;
  }
  if (has_lower && has_upper) return MakeError(ErrorCode::kInvalidArgument, "mixed case address");
  std::string lower = to_lower(addr);
  const auto pos = lower.rfind('1');
  if (pos == std::string::npos || pos < 1 || pos + 7 > lower.size()) {
    return MakeError(ErrorCode::kInvalidArgument, "invalid separator position");
  }
  std::string hrp = lower.substr(0, pos);
  std::vector<std::uint8_t> data;
  for (std::size_t i = pos + 1; i < lower.size(); ++i) {
    const char* p = std::find(std::begin(kCharset), std::end(kCharset) - 1, lower[i]);
    if (p == std::end(kCharset) - 1) return MakeError(ErrorCode::kInvalidArgument, "invalid bech32 character");
    data.push_back(static_cast<std::uint8_t>(p - std::begin(kCharset)));
  }
  if (!verify_checksum(hrp, data)) return MakeError(ErrorCode::kInvalidArgument, "invalid bech32m checksum");
  data.resize(data.size() - 6);
  return std::make_pair(hrp, data);
}

Result<std::string> encode_phantomx_address(std::string_view hrp, std::uint8_t version, std::span<const std::uint8_t> payload) {
  if (version > 31) return MakeError(ErrorCode::kInvalidArgument, "address version out of range");
  std::vector<std::uint8_t> data5;
  data5.push_back(version);
  auto converted = convert_bits(payload, 8, 5, true);
  if (converted.empty() && !payload.empty()) return MakeError(ErrorCode::kInvalidArgument, "payload conversion failed");
  data5.insert(data5.end(), converted.begin(), converted.end());
  return bech32m_encode(hrp, data5);
}

Result<std::pair<std::uint8_t, std::vector<std::uint8_t>>> decode_phantomx_address(std::string_view hrp_expected, std::string_view addr) {
  auto decoded = bech32m_decode(addr);
  if (!decoded.ok()) return decoded.error();
  if (decoded.value().first != to_lower(std::string(hrp_expected))) {
    return MakeError(ErrorCode::kInvalidArgument, "unexpected address hrp");
  }
  const auto& data5 = decoded.value().second;
  if (data5.empty()) return MakeError(ErrorCode::kInvalidArgument, "empty address payload");
  const std::uint8_t version = data5[0];
  std::vector<std::uint8_t> payload5(data5.begin() + 1, data5.end());
  auto payload = convert_bits(payload5, 5, 8, false);
  if (payload.empty() && !payload5.empty()) return MakeError(ErrorCode::kInvalidArgument, "payload conversion failed");
  return std::make_pair(version, payload);
}

}  // namespace phantomx
