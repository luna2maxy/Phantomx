#include "phantomx/common/types.hpp"

#include <charconv>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace phantomx {

std::string Version::to_string() const {
  return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

std::string atoms_to_phx_string(PhxAtom atoms) {
  const PhxAtom whole = atoms / kPhxAtomsPerPhx;
  const PhxAtom frac = atoms % kPhxAtomsPerPhx;
  std::ostringstream oss;
  oss << whole << '.' << std::setw(8) << std::setfill('0') << frac;
  return oss.str();
}

bool parse_phx_atoms(std::string_view text, PhxAtom& out_atoms) {
  if (text.empty()) return false;
  std::string s(text);
  // Trim whitespace
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  while (!s.empty() && !not_space(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && !not_space(static_cast<unsigned char>(s.back()))) s.pop_back();
  if (s.empty()) return false;

  const auto dot = s.find('.');
  std::string whole_part = dot == std::string::npos ? s : s.substr(0, dot);
  std::string frac_part = dot == std::string::npos ? "" : s.substr(dot + 1);
  if (whole_part.empty()) whole_part = "0";
  if (frac_part.size() > 8) return false;
  for (char c : whole_part) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  for (char c : frac_part) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  while (frac_part.size() < 8) frac_part.push_back('0');

  std::uint64_t whole = 0;
  std::uint64_t frac = 0;
  auto [p1, ec1] = std::from_chars(whole_part.data(), whole_part.data() + whole_part.size(), whole);
  auto [p2, ec2] = std::from_chars(frac_part.data(), frac_part.data() + frac_part.size(), frac);
  if (ec1 != std::errc{} || ec2 != std::errc{}) return false;
  if (p1 != whole_part.data() + whole_part.size()) return false;
  if (p2 != frac_part.data() + frac_part.size()) return false;
  if (whole > (UINT64_MAX - frac) / kPhxAtomsPerPhx) return false;
  out_atoms = whole * kPhxAtomsPerPhx + frac;
  return true;
}

}  // namespace phantomx
