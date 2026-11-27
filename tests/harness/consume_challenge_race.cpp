#include "phantomx/blockchain/chain.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  if (argc < 10) {
    std::cerr << "usage: consume_challenge_race <db> <challenge_id> <verifier_id> <pubkey_hex> <owner> <network> <now_ts> <barrier_file> <result_file>\n";
    return 2;
  }
  const std::filesystem::path db_path = argv[1];
  const std::string challenge_id = argv[2];
  const std::string verifier_id = argv[3];
  const std::string pubkey_hex = argv[4];
  const std::string owner = argv[5];
  const std::string network = argv[6];
  const std::uint64_t now_ts = static_cast<std::uint64_t>(std::stoull(argv[7]));
  const std::filesystem::path barrier = argv[8];
  const std::filesystem::path result = argv[9];

  auto db = phantomx::blockchain::ChainDatabase::open(db_path);
  if (!db.ok()) {
    std::ofstream out(result, std::ios::trunc);
    out << "err\n" << db.error().to_string() << "\n";
    return 3;
  }

  for (int i = 0; i < 30000; ++i) {
    if (std::filesystem::exists(barrier)) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  auto r = db.value().consume_verifier_challenge(challenge_id, verifier_id, pubkey_hex, owner, network, now_ts);
  std::ofstream out(result, std::ios::trunc);
  if (!r.ok()) {
    out << "err\n" << r.error().to_string() << "\n";
    (void)db.value().close();
    return 4;
  }
  out << (r.value() ? "ok" : "no") << "\n";
  (void)db.value().close();
  return r.value() ? 0 : 1;
}
