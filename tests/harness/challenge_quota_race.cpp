#include "phantomx/blockchain/chain.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using phantomx::blockchain::ChainDatabase;

static ChainDatabase::VerifierChallengeRecord mkrec(const std::string& id, const std::string& vid, const std::string& owner, std::uint64_t now) {
  ChainDatabase::VerifierChallengeRecord r{};
  r.challenge_id = id;
  r.verifier_id = vid;
  r.pubkey_hex = std::string(64, '1');
  r.owner = owner;
  r.network = "regtest";
  r.payload_blob = {1,2,3};
  r.issued_at = now;
  r.expires_at = now + 300;
  r.status = "issued";
  return r;
}

int main(int argc, char** argv) {
  // usage: challenge_quota_race <db> <mode:per|global> <initial> <threads> <now> <barrier> <result_dir>
  if (argc < 8) { std::cerr << "usage\n"; return 2; }
  std::filesystem::path db = argv[1];
  std::string mode = argv[2];
  int initial = std::stoi(argv[3]);
  int threads_n = std::stoi(argv[4]);
  std::uint64_t now = std::stoull(argv[5]);
  std::filesystem::path barrier = argv[6];
  std::filesystem::path outdir = argv[7];
  std::filesystem::create_directories(outdir);

  std::filesystem::remove(db);
  auto c = ChainDatabase::open(db);
  if (!c.ok()) { std::cerr << c.error().to_string(); return 3; }
  for (int i = 0; i < initial; ++i) {
    std::string cid(64, 'a');
    // deterministic unique id
    for (int k = 0; k < 8; ++k) cid[k] = static_cast<char>('a' + ((i >> (k*4)) & 0xF));
    std::string vid = (mode == "per") ? "v1" : ("v" + std::to_string(i));
    std::string owner = (mode == "per") ? "o1" : ("o" + std::to_string(i));
    auto r = mkrec(cid, vid, owner, now);
    auto pr = c.value().put_verifier_challenge(r);
    if (!pr.ok()) { std::cerr << "seed fail: " << pr.error().to_string(); return 4; }
  }
  (void)c.value().close();

  std::atomic<int> ok_count{0}, reject_count{0}, err_count{0};
  std::vector<std::thread> threads;
  threads.reserve(static_cast<size_t>(threads_n));
  for (int i = 0; i < threads_n; ++i) {
    threads.emplace_back([&, i]{
      auto dbh = ChainDatabase::open(db);
      if (!dbh.ok()) {
        err_count++;
        std::ofstream out(outdir / ("res_" + std::to_string(i) + ".txt"), std::ios::trunc);
        out << "err\n" << dbh.error().to_string() << "\n";
        return;
      }
      for (int w = 0; w < 30000; ++w) {
        if (std::filesystem::exists(barrier)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      std::string cid(64, 'f');
      for (int k = 0; k < 8; ++k) cid[k] = static_cast<char>('a' + ((i >> (k*4)) & 0xF));
      cid[8] = 'x';
      std::string vid = (mode == "per") ? "v1" : ("nv" + std::to_string(i));
      std::string owner = (mode == "per") ? "o1" : ("no" + std::to_string(i));
      auto r = mkrec(cid, vid, owner, now);
      std::uint64_t global_cap = (mode == "per") ? 4096 : 4096;
      std::uint64_t per_cap = 8;
      auto pr = dbh.value().put_verifier_challenge_with_quota(r, now, global_cap, per_cap, 3600, 256);
      std::ofstream out(outdir / ("res_" + std::to_string(i) + ".txt"), std::ios::trunc);
      if (pr.ok()) { ok_count++; out << "ok\n"; }
      else {
        const auto& m = pr.error().message();
        if (m == "challenge_capacity_global" || m == "challenge_capacity_identity") { reject_count++; out << "reject\n" << m << "\n"; }
        else { err_count++; out << "err\n" << pr.error().to_string() << "\n"; }
      }
      (void)dbh.value().close();
    });
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  { std::ofstream b(barrier, std::ios::trunc); b << "go"; }
  for (auto& t : threads) t.join();

  auto c2 = ChainDatabase::open(db);
  if (!c2.ok()) { std::cerr << c2.error().to_string(); return 5; }
  auto active_global = c2.value().count_active_verifier_challenges(now);
  std::uint64_t active_identity = 0;
  if (mode == "per") {
    auto ai = c2.value().count_active_verifier_challenges_for("v1", "o1", now);
    if (!ai.ok()) { std::cerr << ai.error().to_string(); return 6; }
    active_identity = ai.value();
  }
  (void)c2.value().close();
  if (!active_global.ok()) { std::cerr << active_global.error().to_string(); return 7; }

  std::cout << "ok=" << ok_count.load()
            << " reject=" << reject_count.load()
            << " err=" << err_count.load()
            << " global=" << active_global.value()
            << " identity=" << active_identity << "\n";
  if (err_count.load() != 0) return 10;
  if (mode == "per") {
    if (ok_count.load() != 1) return 11;
    if (reject_count.load() != threads_n - 1) return 12;
    if (active_identity > 8) return 13;
    if (active_global.value() != static_cast<std::uint64_t>(initial + 1)) return 14;
  } else {
    if (ok_count.load() != 1) return 21;
    if (reject_count.load() != threads_n - 1) return 22;
    if (active_global.value() > 4096) return 23;
  }
  return 0;
}
