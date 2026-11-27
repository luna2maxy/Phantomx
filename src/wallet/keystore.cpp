#include "phantomx/wallet/keystore.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace phantomx::wallet {

namespace {

constexpr const char* kDefaultAccount = "default";

bool is_label_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' || c == '.';
}

}  // namespace

bool Keystore::valid_label(const std::string& label) {
  if (label.empty() || label.size() > 64) return false;
  return std::all_of(label.begin(), label.end(), is_label_char);
}

Result<void> Keystore::generate(crypto::KeyRole role) {
  auto kp = crypto::generate_keypair(role);
  if (!kp.ok()) return kp.error();
  keys_[static_cast<std::uint8_t>(role)] = kp.value();
  if (role == crypto::KeyRole::kWalletSigning) {
    accounts_[kDefaultAccount] = kp.value();
  }
  return Result<void>{};
}

Result<void> Keystore::import_secret_hex(crypto::KeyRole role, std::string_view secret_hex) {
  auto sec = crypto::parse_secret_key_hex(secret_hex);
  if (!sec.ok()) return sec.error();
  // Derive public key by using libsodium's secret key format (last 32 bytes are pubkey in crypto_sign).
  crypto::KeyPair kp;
  kp.role = role;
  kp.sec = sec.value();
  std::copy(kp.sec.bytes.end() - 32, kp.sec.bytes.end(), kp.pub.bytes.begin());
  keys_[static_cast<std::uint8_t>(role)] = kp;
  if (role == crypto::KeyRole::kWalletSigning) {
    accounts_[kDefaultAccount] = kp;
  }
  return Result<void>{};
}

bool Keystore::has(crypto::KeyRole role) const {
  if (role == crypto::KeyRole::kWalletSigning && !accounts_.empty()) return true;
  return keys_.find(static_cast<std::uint8_t>(role)) != keys_.end();
}

std::optional<crypto::KeyPair> Keystore::get(crypto::KeyRole role) const {
  if (role == crypto::KeyRole::kWalletSigning) {
    auto it = accounts_.find(kDefaultAccount);
    if (it != accounts_.end()) return it->second;
    if (!accounts_.empty()) return accounts_.begin()->second;
  }
  auto it = keys_.find(static_cast<std::uint8_t>(role));
  if (it == keys_.end()) return std::nullopt;
  return it->second;
}

Result<void> Keystore::generate_account(const std::string& label) {
  if (!valid_label(label)) return MakeError(ErrorCode::kInvalidArgument, "invalid account label: " + label);
  auto kp = crypto::generate_keypair(crypto::KeyRole::kWalletSigning);
  if (!kp.ok()) return kp.error();
  accounts_[label] = kp.value();
  if (label == kDefaultAccount) {
    keys_[static_cast<std::uint8_t>(crypto::KeyRole::kWalletSigning)] = kp.value();
  }
  return Result<void>{};
}

Result<void> Keystore::import_account(const std::string& label, std::string_view secret_hex) {
  if (!valid_label(label)) return MakeError(ErrorCode::kInvalidArgument, "invalid account label: " + label);
  auto sec = crypto::parse_secret_key_hex(secret_hex);
  if (!sec.ok()) return sec.error();
  crypto::KeyPair kp;
  kp.role = crypto::KeyRole::kWalletSigning;
  kp.sec = sec.value();
  std::copy(kp.sec.bytes.end() - 32, kp.sec.bytes.end(), kp.pub.bytes.begin());
  accounts_[label] = kp;
  if (label == kDefaultAccount) {
    keys_[static_cast<std::uint8_t>(crypto::KeyRole::kWalletSigning)] = kp;
  }
  return Result<void>{};
}

bool Keystore::has_account(const std::string& label) const {
  return accounts_.find(label) != accounts_.end();
}

std::optional<crypto::KeyPair> Keystore::get_account(const std::string& label) const {
  auto it = accounts_.find(label);
  if (it == accounts_.end()) return std::nullopt;
  return it->second;
}

std::vector<std::string> Keystore::account_labels() const {
  std::vector<std::string> labels;
  labels.reserve(accounts_.size());
  for (const auto& [label, _] : accounts_) labels.push_back(label);
  return labels;
}

std::vector<WalletAccount> Keystore::accounts() const {
  std::vector<WalletAccount> out;
  out.reserve(accounts_.size());
  for (const auto& [label, kp] : accounts_) out.push_back(WalletAccount{label, kp});
  return out;
}

Result<void> Keystore::save(const std::filesystem::path& path) const {
  const auto tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out.is_open()) return MakeError(ErrorCode::kIoError, "unable to open temporary keystore for writing");
    out << "# PhantomX keystore (plaintext secret keys). Protect this file.\n";
    out << "version=2\n";
    for (const auto& [label, kp] : accounts_) {
      out << "account." << label << ".public=" << crypto::serialize_public_key_hex(kp.pub) << "\n";
      out << "account." << label << ".secret=" << crypto::serialize_secret_key_hex(kp.sec) << "\n";
    }
    for (const auto& [role_u8, kp] : keys_) {
      const auto role = static_cast<crypto::KeyRole>(role_u8);
      if (role == crypto::KeyRole::kWalletSigning) continue;
      out << crypto::to_string(role) << ".role=" << static_cast<int>(role_u8) << "\n";
      out << crypto::to_string(role) << ".public=" << crypto::serialize_public_key_hex(kp.pub) << "\n";
      out << crypto::to_string(role) << ".secret=" << crypto::serialize_secret_key_hex(kp.sec) << "\n";
    }
    auto def = accounts_.find(kDefaultAccount);
    if (def == accounts_.end() && !accounts_.empty()) def = accounts_.begin();
    if (def != accounts_.end()) {
      const auto& kp = def->second;
      out << "wallet_signing.role=" << static_cast<int>(crypto::KeyRole::kWalletSigning) << "\n";
      out << "wallet_signing.public=" << crypto::serialize_public_key_hex(kp.pub) << "\n";
      out << "wallet_signing.secret=" << crypto::serialize_secret_key_hex(kp.sec) << "\n";
    }
    out.flush();
    if (!out.good()) return MakeError(ErrorCode::kIoError, "unable to write keystore");
  }
#if !defined(_WIN32)
  if (::chmod(tmp.c_str(), S_IRUSR | S_IWUSR) != 0) {
    std::error_code ec; std::filesystem::remove(tmp, ec);
    return MakeError(ErrorCode::kIoError, "unable to protect temporary keystore");
  }
#endif
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    return MakeError(ErrorCode::kIoError, "unable to replace keystore");
  }
#if !defined(_WIN32)
  ::chmod(path.string().c_str(), S_IRUSR | S_IWUSR);
#endif
  return Result<void>{};
}

Result<Keystore> Keystore::load(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in.is_open()) return MakeError(ErrorCode::kIoError, "unable to open keystore for reading");
  Keystore ks;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    auto key = line.substr(0, eq);
    auto value = line.substr(eq + 1);

    // New format: account.<label>.secret=...
    if (key.rfind("account.", 0) == 0) {
      const auto rest = key.substr(8);  // after "account."
      auto dot = rest.rfind('.');
      if (dot == std::string::npos) continue;
      const auto label = rest.substr(0, dot);
      const auto field = rest.substr(dot + 1);
      if (field != "secret") continue;
      auto imported = ks.import_account(label, value);
      if (!imported.ok()) return imported.error();
      continue;
    }

    // Legacy format: <role_name>.secret=...
    auto dot = key.rfind('.');
    if (dot == std::string::npos) continue;
    auto role_name = key.substr(0, dot);
    auto field = key.substr(dot + 1);
    if (field != "secret") continue;
    crypto::KeyRole role = crypto::KeyRole::kWalletSigning;
    if (role_name == "wallet_signing") role = crypto::KeyRole::kWalletSigning;
    else if (role_name == "node_identity") role = crypto::KeyRole::kNodeIdentity;
    else if (role_name == "verifier_identity") role = crypto::KeyRole::kVerifierIdentity;
    else continue;
    auto imported = ks.import_secret_hex(role, value);
    if (!imported.ok()) return imported.error();
  }
  // Ensure default account exists if legacy key was loaded.
  if (ks.accounts_.empty()) {
    auto it = ks.keys_.find(static_cast<std::uint8_t>(crypto::KeyRole::kWalletSigning));
    if (it != ks.keys_.end()) ks.accounts_[kDefaultAccount] = it->second;
  }
  return ks;
}

}  // namespace phantomx::wallet