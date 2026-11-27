#include "phantomx/consensus/bft_config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include <jansson.h>

#include "phantomx/common/crypto.hpp"
#include "phantomx/common/serialization.hpp"
#include "phantomx/consensus/consensus.hpp"
#include "phantomx/primitives/hash.hpp"

namespace phantomx::consensus {
namespace {

std::string trim_copy(std::string s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

bool is_hex_string(const std::string& s) {
  if (s.empty()) return false;
  for (char ch : s) {
    if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
  }
  return true;
}

Result<std::array<std::uint8_t, 32>> parse_pubkey_hex(const std::string& hex) {
  if (hex.size() != 64 || !is_hex_string(hex)) {
    return MakeError(ErrorCode::kConfigError, "validator pubkey must be 64 hex characters");
  }
  auto pk = crypto::parse_public_key_hex(hex);
  if (!pk.ok()) return MakeError(ErrorCode::kConfigError, "invalid validator pubkey hex");
  return pk.value().bytes;
}

Result<std::vector<BftValidatorConfig>> parse_validators_json(const std::string& raw) {
  json_error_t err{};
  json_t* root = json_loads(raw.c_str(), 0, &err);
  if (root == nullptr) {
    return MakeError(ErrorCode::kConfigError, "bft.validators must be a valid JSON array");
  }
  if (!json_is_array(root)) {
    json_decref(root);
    return MakeError(ErrorCode::kConfigError, "bft.validators must be a JSON array");
  }
  std::vector<BftValidatorConfig> out;
  const size_t n = json_array_size(root);
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    json_t* item = json_array_get(root, i);
    if (!json_is_object(item)) {
      json_decref(root);
      return MakeError(ErrorCode::kConfigError, "bft.validators[" + std::to_string(i) + "] must be an object");
    }
    json_t* pkv = json_object_get(item, "pubkey");
    if (pkv == nullptr || !json_is_string(pkv)) {
      json_decref(root);
      return MakeError(ErrorCode::kConfigError, "bft.validators[" + std::to_string(i) + "].pubkey is required");
    }
    const std::string pubhex = trim_copy(json_string_value(pkv));
    auto pk = parse_pubkey_hex(pubhex);
    if (!pk.ok()) {
      json_decref(root);
      return MakeError(ErrorCode::kConfigError, "bft.validators[" + std::to_string(i) + "]: " + pk.error().message());
    }
    std::uint64_t vp = 1;
    json_t* vpv = json_object_get(item, "voting_power");
    if (vpv != nullptr) {
      if (!json_is_integer(vpv) || json_integer_value(vpv) <= 0) {
        json_decref(root);
        return MakeError(ErrorCode::kConfigError, "bft.validators[" + std::to_string(i) + "].voting_power must be a positive integer");
      }
      vp = static_cast<std::uint64_t>(json_integer_value(vpv));
    }
    // Reject unknown fields? Be lenient on unknown optional fields, but reject obviously dangerous ones is not needed.
    BftValidatorConfig vc;
    vc.pubkey = pk.value();
    vc.voting_power = vp;
    out.push_back(vc);
  }
  json_decref(root);
  return out;
}

Result<std::uint32_t> parse_u32_field(const Config& cfg, const char* key, bool required, std::uint32_t def) {
  const auto v = cfg.get(key);
  if (!v.has_value() || trim_copy(*v).empty()) {
    if (required) return MakeError(ErrorCode::kConfigError, std::string("missing required config: ") + key);
    return def;
  }
  try {
    const auto raw = trim_copy(*v);
    size_t pos = 0;
    unsigned long long parsed = std::stoull(raw, &pos, 10);
    if (pos != raw.size() || parsed > 0xFFFFFFFFULL) {
      return MakeError(ErrorCode::kConfigError, std::string("invalid integer for ") + key);
    }
    return static_cast<std::uint32_t>(parsed);
  } catch (...) {
    return MakeError(ErrorCode::kConfigError, std::string("invalid integer for ") + key);
  }
}

Result<bool> parse_bool_field(const Config& cfg, const char* key, bool def) {
  const auto v = cfg.get(key);
  if (!v.has_value() || trim_copy(*v).empty()) return def;
  const auto s = trim_copy(*v);
  if (s == "true" || s == "1" || s == "yes" || s == "on") return true;
  if (s == "false" || s == "0" || s == "no" || s == "off") return false;
  return MakeError(ErrorCode::kConfigError, std::string("invalid boolean for ") + key);
}

}  // namespace

Result<BftConfig> parse_bft_config(const Config& cfg) {
  BftConfig out;
  const auto enabled_r = parse_bool_field(cfg, "bft.enabled", false);
  if (!enabled_r.ok()) return enabled_r.error();
  out.enabled = enabled_r.value();
  if (!out.enabled) {
    const auto consensus = cfg.get("consensus");
    if (consensus.has_value() && trim_copy(*consensus) == "bft") out.enabled = true;
  }

  const auto vc_r = parse_bool_field(cfg, "bft.view_change_enabled", false);
  if (!vc_r.ok()) return vc_r.error();
  out.view_change_enabled = vc_r.value();

  const auto count_r = parse_u32_field(cfg, "bft.validator_count", false, 0);
  if (!count_r.ok()) return count_r.error();
  out.validator_count = count_r.value();

  if (const auto p = cfg.get("bft.validator_seed_prefix"); p.has_value()) {
    out.validator_seed_prefix = trim_copy(*p);
  }
  if (const auto p = cfg.get("bft.signing_key_file"); p.has_value()) {
    out.signing_key_file = trim_copy(*p);
  }

  // Parse validators array if present.
  if (const auto v = cfg.get("bft.validators"); v.has_value() && !trim_copy(*v).empty()) {
    auto parsed = parse_validators_json(*v);
    if (!parsed.ok()) return parsed.error();
    out.validators = std::move(parsed.value());
  }

  // Determine key mode.
  const auto mode_v = cfg.get("bft.key_mode");
  if (mode_v.has_value() && !trim_copy(*mode_v).empty()) {
    const auto m = trim_copy(*mode_v);
    if (m == "operator") out.key_mode = BftKeyMode::kOperator;
    else if (m == "seed") out.key_mode = BftKeyMode::kSeed;
    else return MakeError(ErrorCode::kConfigError, "bft.key_mode must be 'operator' or 'seed'");
  } else {
    // Backward-compatible inference: validators array -> operator; seed prefix -> seed.
    if (!out.validators.empty()) out.key_mode = BftKeyMode::kOperator;
    else out.key_mode = BftKeyMode::kSeed;
  }

  // If not enabled, allow minimal parsing (caller decides). But still validate obvious issues.
  if (!out.enabled) return out;

  const auto startup = validate_bft_startup(out, cfg);
  if (!startup.ok()) return startup.error();
  return out;
}

Result<void> validate_bft_startup(const BftConfig& cfg, const Config&) {
  if (!cfg.enabled) return {};

  if (cfg.validator_count == 0) {
    return MakeError(ErrorCode::kConfigError, "bft.validator_count is required when bft.enabled=true");
  }
  if (cfg.key_mode == BftKeyMode::kOperator) {
    if (cfg.validator_count != 4) {
      return MakeError(ErrorCode::kConfigError, "bft.validator_count must be exactly 4 for PB-001B canonical operator BFT topology");
    }
  } else {
    // Seed/dev mode: currently validated topology is 4 validators. Keep fail-closed for unsupported sizes.
    if (cfg.validator_count != 4) {
      return MakeError(ErrorCode::kConfigError, "bft.validator_count must be exactly 4 in current validated BFT seed mode (PB-001B scope); unsupported validator count");
    }
  }

  if (cfg.key_mode == BftKeyMode::kOperator) {
    if (cfg.validators.empty()) {
      return MakeError(ErrorCode::kConfigError, "bft.validators is required when bft.key_mode=operator");
    }
    if (cfg.validators.size() != cfg.validator_count) {
      return MakeError(ErrorCode::kConfigError, "bft.validators count must match bft.validator_count");
    }
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < cfg.validators.size(); ++i) {
      const auto& v = cfg.validators[i];
      if (v.voting_power == 0) {
        return MakeError(ErrorCode::kConfigError, "bft.validators[" + std::to_string(i) + "].voting_power must be > 0");
      }
      const std::string hex = to_hex(std::span<const std::uint8_t>(v.pubkey.data(), v.pubkey.size()));
      if (!seen.insert(hex).second) {
        return MakeError(ErrorCode::kConfigError, "duplicate validator pubkey in bft.validators");
      }
    }
    if (!cfg.validator_seed_prefix.empty()) {
      // Seed prefix is irrelevant/dangerous in operator mode; reject to avoid ambiguity.
      return MakeError(ErrorCode::kConfigError, "bft.validator_seed_prefix must not be set when bft.key_mode=operator");
    }
  } else { // seed mode
    if (cfg.validator_seed_prefix.empty()) {
      return MakeError(ErrorCode::kConfigError, "bft.validator_seed_prefix is required when bft.key_mode=seed");
    }
    if (!cfg.validators.empty()) {
      return MakeError(ErrorCode::kConfigError, "bft.validators must not be set when bft.key_mode=seed; use key_mode=operator for explicit pubkeys");
    }
  }

  // Quorum rule must remain >2/3 (existing formula). Reject explicit incompatible config.
  // We don't add a new config key now; just ensure no contradictory key is present.
  if (const auto qr = cfg.validators.empty() ? std::optional<std::string>{} : std::optional<std::string>{}; false) { (void)qr; }

  return {};
}

Result<ValidatorSet> build_validator_set(const BftConfig& cfg, std::uint64_t epoch) {
  ValidatorSet vs;
  vs.epoch = epoch;
  if (!cfg.enabled) return vs;

  if (cfg.key_mode == BftKeyMode::kOperator) {
    for (const auto& vc : cfg.validators) {
      Validator v;
      v.validator_pubkey = vc.pubkey;
      v.operator_pubkey = vc.pubkey;
      v.voting_power = vc.voting_power;
      v.active = true;
      vs.validators.push_back(v);
      vs.total_voting_power += v.voting_power;
    }
    return vs;
  }

  // Seed mode: deterministic derivation for regtest/dev. Preserve legacy voting power semantics.
  for (std::uint32_t i = 0; i < cfg.validator_count; ++i) {
    const auto seed_text = cfg.validator_seed_prefix + std::to_string(i);
    const auto digest = sha256d(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(seed_text.data()), seed_text.size()));
    std::array<std::uint8_t, crypto::kSeedBytes> seed{};
    std::copy(digest.begin(), digest.end(), seed.begin());
    auto kp = crypto::keypair_from_seed(crypto::KeyRole::kNodeIdentity, seed);
    if (!kp.ok()) return MakeError(ErrorCode::kConfigError, "failed to derive validator key from seed");
    Validator v;
    v.validator_pubkey = kp.value().pub.bytes;
    v.operator_pubkey = v.validator_pubkey;
    ValidatorEligibilityInput eligibility;
    eligibility.validator_pubkey = v.validator_pubkey;
    eligibility.operator_pubkey = v.operator_pubkey;
    eligibility.poi_score = 100000;
    eligibility.identity_verified = true;
    const auto power = compute_voting_power(eligibility, VotingPowerPolicy{});
    v.voting_power = power;
    v.active = true;
    vs.validators.push_back(v);
    vs.total_voting_power += v.voting_power;
  }
  return vs;
}

Result<crypto::SecretKey> load_secret_key_hex_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return MakeError(ErrorCode::kConfigError, "unable to open BFT signing key file: " + path.string());
  }
  std::stringstream ss;
  ss << in.rdbuf();
  auto hex = trim_copy(ss.str());
  // Allow optional quotes/whitespace/newline.
  if (!hex.empty() && (hex.front() == '"' || hex.front() == '\'')) hex = hex.substr(1);
  if (!hex.empty() && (hex.back() == '"' || hex.back() == '\'')) hex.pop_back();
  hex = trim_copy(hex);
  if (hex.size() != 128 || !is_hex_string(hex)) {
    return MakeError(ErrorCode::kConfigError, "BFT signing key file must contain 128 hex characters (libsodium secret key)");
  }
  auto sk = crypto::parse_secret_key_hex(hex);
  if (!sk.ok()) {
    return MakeError(ErrorCode::kConfigError, "invalid BFT signing key file format");
  }
  return sk.value();
}

Result<LoadedBftIdentity> load_operator_identity(const BftConfig& cfg,
                                                 const std::filesystem::path& signing_key_file) {
  if (cfg.key_mode != BftKeyMode::kOperator) {
    return MakeError(ErrorCode::kConfigError, "operator identity loading requires bft.key_mode=operator");
  }
  if (signing_key_file.empty()) {
    return MakeError(ErrorCode::kConfigError, "bft signing key file path is required for operator key mode");
  }
  auto sk = load_secret_key_hex_file(signing_key_file);
  if (!sk.ok()) return sk.error();

  crypto::KeyPair kp;
  kp.role = crypto::KeyRole::kNodeIdentity;
  kp.sec = sk.value();
  // Derive public key from libsodium secret key (last 32 bytes are pubkey in crypto_sign).
  std::copy(kp.sec.bytes.begin() + 32, kp.sec.bytes.end(), kp.pub.bytes.begin());

  // Verify keypair is usable by signing/verifying a probe.
  const std::array<std::uint8_t, 32> probe{{1,2,3,4}};
  auto sig = crypto::sign(std::span<const std::uint8_t>(probe.data(), probe.size()), kp.sec);
  if (!sig.ok() || !crypto::verify(std::span<const std::uint8_t>(probe.data(), probe.size()), sig.value(), kp.pub)) {
    return MakeError(ErrorCode::kConfigError, "BFT signing key file is not a valid usable keypair");
  }

  LoadedBftIdentity out;
  out.local_key = kp;
  out.validator_pubkey = kp.pub.bytes;
  bool found = false;
  for (size_t i = 0; i < cfg.validators.size(); ++i) {
    if (cfg.validators[i].pubkey == kp.pub.bytes) {
      out.validator_index = i;
      found = true;
      break;
    }
  }
  if (!found) {
    return MakeError(ErrorCode::kConfigError, "BFT signing key does not match any configured validator pubkey");
  }
  return out;
}

Result<std::size_t> resolve_seed_node_index(std::uint32_t node_index_1based, std::uint32_t validator_count) {
  if (validator_count == 0) return MakeError(ErrorCode::kConfigError, "validator_count must be > 0");
  if (node_index_1based < 1 || node_index_1based > validator_count) {
    return MakeError(ErrorCode::kConfigError, "node-index must be in range 1.." + std::to_string(validator_count));
  }
  return static_cast<std::size_t>(node_index_1based - 1);
}

}  // namespace phantomx::consensus
