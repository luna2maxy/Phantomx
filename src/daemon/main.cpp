#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "phantomx/aggregator/aggregator.hpp"
#include "phantomx/blockchain/chain.hpp"
#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/common/cli.hpp"
#include "phantomx/primitives/block.hpp"
#include "phantomx/common/config.hpp"
#include "phantomx/common/crypto.hpp"
#include "phantomx/common/logging.hpp"
#include "phantomx/common/paths.hpp"
#include "phantomx/consensus/consensus.hpp"
#include "phantomx/consensus/bft.hpp"
#include "phantomx/consensus/bft_config.hpp"
#include "phantomx/network/node.hpp"
#include "phantomx/pool/pool.hpp"
#include "phantomx/rpc/http_server.hpp"
#include "phantomx/rpc/node_handlers.hpp"
#include "phantomx/rpc/rpc.hpp"
#include "phantomx/utxo/mempool.hpp"

namespace {
constexpr const char* kName = "phantomxd";
constexpr const char* kVersion = "0.1.0";
constexpr std::uint64_t kMaxViewJump = 16;
std::uint16_t get_port(const phantomx::ParsedArgs& a, const char* key, std::uint16_t fallback) {
  const auto it = a.options.find(key);
  return it == a.options.end() || it->second.empty() ? fallback : static_cast<std::uint16_t>(std::stoul(it->second));
}
}

namespace phantomx::rpc {
void set_rpc_allow_ips(std::vector<std::string> ips);
Result<void> serve_rpc(RpcServer& server, const std::string& bind_host, std::uint16_t port, const std::vector<std::string>& allow_ips);
}  // namespace phantomx::rpc

int main(int argc, char* argv[]) {
#ifndef _WIN32
  // A peer that closes mid-write must not terminate the daemon with SIGPIPE.
  std::signal(SIGPIPE, SIG_IGN);
#endif
  phantomx::CliParser parser(kName);
  parser.add_option("--config", "Path to config file");
  parser.add_option("--datadir", "Node data directory (default: network datadir)");
  parser.add_option("--rpc-host", "RPC bind host");
  parser.add_option("--rpc-port", "RPC bind port");
  parser.add_option("--p2p-host", "P2P bind host");
  parser.add_option("--p2p-port", "P2P bind port");
  parser.add_option("--node-index", "Deterministic validator node index (1..4)", "1");
  parser.add_option("--connect", "Peer host:port");
  parser.add_option("--rpc-token", "RPC authentication token");
  parser.add_option("--rpc-allowip", "Allowed RPC client IP (repeatable/comma-separated)");
  parser.add_option("--bft-signing-key-file", "Path to local BFT validator signing key hex file (operator key mode)");
  parser.add_option("--log-level", "Log level", "info");
  parser.add_flag("--testnet", "Use testnet");
  parser.add_flag("--regtest", "Use regtest");
  parser.add_flag("--help", "Show help");
  parser.add_flag("--version", "Show version");
  auto parsed = parser.parse(argc, argv);
  if (!parsed.ok()) { std::cerr << parsed.error().to_string() << "\n" << parser.help_text(); return 2; }
  auto args = parsed.value();

  auto has_flag = [&](const char* f) { return std::find(args.flags.begin(), args.flags.end(), f) != args.flags.end(); };
  if (has_flag("--help")) { std::cout << parser.help_text(); return 0; }

  auto opt = [&](const char* k) -> std::optional<std::string> {
    auto it = args.options.find(k);
    if (it == args.options.end() || it->second.empty()) return std::nullopt;
    return it->second;
  };

  // Bootstrap config to detect network when --config is not provided.
  const bool cli_testnet = has_flag("--testnet");
  const bool cli_regtest = has_flag("--regtest");
  if (cli_testnet && cli_regtest) { std::cerr << "testnet and regtest cannot both be enabled\n"; return 2; }

  std::filesystem::path bootstrap_config;
  if (auto c = opt("--config"); c.has_value()) bootstrap_config = *c;
  else bootstrap_config = phantomx::default_config_path(phantomx::default_datadir(phantomx::Network::kMainnet));

  std::optional<std::string> cfg_testnet, cfg_regtest, cfg_chain;
  if (std::filesystem::exists(bootstrap_config)) {
    auto boot_cfg = phantomx::Config::load_file(bootstrap_config);
    if (boot_cfg.ok()) {
      cfg_testnet = boot_cfg.value().get("testnet");
      cfg_regtest = boot_cfg.value().get("regtest");
      cfg_chain = boot_cfg.value().get("chain");
    }
  }
  auto network_r = phantomx::resolve_network(cli_testnet, cli_regtest, cfg_testnet, cfg_regtest, cfg_chain);
  if (!network_r.ok()) { std::cerr << network_r.error().to_string() << "\n"; return 2; }
  const auto network = network_r.value();

  if (has_flag("--version")) {
    std::cout << kName << " " << kVersion << " (" << phantomx::network_name(network) << ")\n";
    return 0;
  }

  const std::filesystem::path datadir = opt("--datadir").has_value() ? std::filesystem::path(*opt("--datadir")) : phantomx::default_datadir(network);
  std::filesystem::create_directories(datadir);

  phantomx::ConfigTemplateOptions tpl;
  tpl.network = network;
  tpl.rpcport = phantomx::default_rpc_port(network);
  tpl.p2pport = phantomx::default_p2p_port(network);
  tpl.rpcbind = "0.0.0.0";
  tpl.rpcallowip = "127.0.0.1";
  auto created_cfg = phantomx::ensure_default_config(datadir, tpl);
  if (!created_cfg.ok()) { std::cerr << created_cfg.error().to_string() << "\n"; return 1; }

  const std::filesystem::path config_path = opt("--config").has_value() ? std::filesystem::path(*opt("--config")) : phantomx::default_config_path(datadir);
  phantomx::Config cfg;
  if (std::filesystem::exists(config_path)) {
    auto loaded = phantomx::Config::load_file(config_path);
    if (!loaded.ok()) { std::cerr << loaded.error().to_string() << "\n"; return 1; }
    cfg = std::move(loaded.value());
  }

  auto cfg_or = [&](std::string_view key, std::string_view def) -> std::string {
    return cfg.get_or(key, def);
  };
  auto split_csv = [](const std::string& v) {
    std::vector<std::string> out;
    std::stringstream ss(v);
    std::string item;
    while (std::getline(ss, item, ',')) {
      const auto first = item.find_first_not_of(" \t\r\n");
      if (first == std::string::npos) continue;
      const auto last = item.find_last_not_of(" \t\r\n");
      out.push_back(item.substr(first, last - first + 1));
    }
    return out;
  };

  const std::string log_level = opt("--log-level").value_or(cfg_or("loglevel", "info"));
  const std::string rpc_host = opt("--rpc-host").value_or(cfg_or("rpcbind", "0.0.0.0"));
  const std::string rpc_port_default = cfg_or("rpcport", cfg_or("protocol.default_rpc_port", std::to_string(phantomx::default_rpc_port(network))));
  const std::string rpc_port_s = opt("--rpc-port").value_or(rpc_port_default);
  const std::string p2p_host = opt("--p2p-host").value_or(cfg_or("p2pbind", "127.0.0.1"));
  const std::string p2p_port_default = cfg_or("port", cfg_or("p2pport", cfg_or("protocol.default_p2p_port", std::to_string(phantomx::default_p2p_port(network)))));
  const std::string p2p_port_s = opt("--p2p-port").value_or(p2p_port_default);
  const std::string rpc_token = opt("--rpc-token").value_or(cfg_or("rpctoken", cfg_or("rpcpassword", "local-dev-token")));
  std::vector<std::string> rpc_allow_ips = split_csv(opt("--rpc-allowip").value_or(cfg_or("rpcallowip", "127.0.0.1")));

  auto logger = phantomx::Logger(kName, std::clog);
  logger.set_level(phantomx::parse_log_level(log_level));
  if (created_cfg.ok() && created_cfg.value()) {
    logger.info("config_created", {{"path", config_path.string()}});
  }
  logger.info("network_selected", {{"network", std::string(phantomx::network_name(network))}, {"datadir", datadir.string()}, {"config", config_path.string()}});

  const bool bft_enabled = cfg_or("bft.enabled", cfg_or("consensus", "poi")) == "bft" || cfg_or("bft.enabled", "false") == "true";
  phantomx::consensus::BftConfig bft_cfg;
  if (bft_enabled) {
    auto bft_cfg_r = phantomx::consensus::parse_bft_config(cfg);
    if (!bft_cfg_r.ok()) {
      logger.error("bft_config_invalid", {{"error", bft_cfg_r.error().to_string()}});
      std::cerr << "invalid BFT config: " << bft_cfg_r.error().to_string() << "\n";
      return 2;
    }
    bft_cfg = std::move(bft_cfg_r.value());
  }
  bool view_change_enabled = bft_enabled && bft_cfg.view_change_enabled;
  logger.info("consensus_mode", {{"mode", bft_enabled ? "poi_bft" : "poi"}, {"bft_enabled", bft_enabled ? "true" : "false"}, {"view_change_enabled", view_change_enabled ? "true" : "false"}});

  if (!phantomx::crypto::init().ok()) return 1;

  // Resolve local BFT/node identity before opening chain to fail closed on invalid config/key.
  phantomx::crypto::KeyPair local_keypair;
  std::size_t local_validator_index = 0;
  const auto node_index_value = std::stoul(args.options.count("--node-index") ? args.options.at("--node-index") : std::string("1"));
  if (bft_enabled && bft_cfg.key_mode == phantomx::consensus::BftKeyMode::kOperator) {
    const std::string signing_key_file = opt("--bft-signing-key-file").value_or(bft_cfg.signing_key_file);
    if (signing_key_file.empty()) {
      logger.error("bft_signing_key_missing", {{"reason", "operator key mode requires --bft-signing-key-file or bft.signing_key_file"}});
      std::cerr << "operator BFT key mode requires --bft-signing-key-file or bft.signing_key_file\n";
      return 2;
    }
    auto id = phantomx::consensus::load_operator_identity(bft_cfg, signing_key_file);
    if (!id.ok()) {
      logger.error("bft_signing_key_invalid", {{"error", id.error().to_string()}});
      std::cerr << "invalid BFT signing key: " << id.error().to_string() << "\n";
      return 2;
    }
    local_keypair = id.value().local_key;
    local_validator_index = id.value().validator_index;
    // If node-index was explicitly provided, require consistency with configured validator set.
    bool node_index_explicit = false;
    for (int i = 1; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--node-index" || a.rfind("--node-index=", 0) == 0) { node_index_explicit = true; break; }
    }
    if (node_index_explicit) {
      if (node_index_value < 1 || node_index_value > bft_cfg.validator_count ||
          static_cast<std::size_t>(node_index_value - 1) != local_validator_index) {
        logger.error("bft_node_index_mismatch", {{"node_index", std::to_string(node_index_value)}, {"validator_index", std::to_string(local_validator_index + 1)}});
        std::cerr << "--node-index does not match the configured validator identity for the provided signing key\n";
        return 2;
      }
    }
    logger.info("bft_operator_identity_loaded", {{"validator_index", std::to_string(local_validator_index + 1)}, {"validator_count", std::to_string(bft_cfg.validator_count)}});
  } else {
    // Seed mode (regtest/dev) or non-BFT node identity. Preserves existing regtest behavior.
    const std::uint32_t vcount = (bft_enabled && bft_cfg.validator_count > 0) ? bft_cfg.validator_count : 4u;
    const std::string seed_prefix = (bft_enabled && !bft_cfg.validator_seed_prefix.empty())
                                        ? bft_cfg.validator_seed_prefix
                                        : std::string("phantomx-regtest-validator-");
    if (node_index_value < 1 || node_index_value > vcount) {
      std::cerr << "--node-index must be in range 1.." << vcount << "\n";
      return 2;
    }
    local_validator_index = static_cast<std::size_t>(node_index_value - 1);
    std::array<std::uint8_t, phantomx::crypto::kSeedBytes> node_seed{};
    const auto node_seed_text = seed_prefix + std::to_string(local_validator_index);
    const auto node_digest = phantomx::sha256d(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(node_seed_text.data()), node_seed_text.size()));
    std::copy(node_digest.begin(), node_digest.end(), node_seed.begin());
    auto key_r = phantomx::crypto::keypair_from_seed(phantomx::crypto::KeyRole::kNodeIdentity, node_seed);
    if (!key_r.ok()) return 1;
    local_keypair = key_r.value();
  }
  auto key = phantomx::Result<phantomx::crypto::KeyPair>(local_keypair);

  auto chain_result = phantomx::blockchain::ChainDatabase::open(datadir / "chain.db");
  if (!chain_result.ok()) { logger.error("chain_open_failed", {{"error", chain_result.error().to_string()}}); return 1; }
  auto chain = std::move(chain_result.value());
  phantomx::Mempool mempool;
  phantomx::pool::PoolLedger pool;
  phantomx::aggregator::AggregationEngine aggregator;
  std::unique_ptr<phantomx::consensus::BftEngine> bft;
  std::recursive_mutex engine_mu;
  std::mutex pending_mu;
  std::condition_variable pending_cv;
  std::vector<phantomx::consensus::BlockProposal> pending_proposals;
  std::vector<phantomx::consensus::BlockProposal> buffered_proposals;
  std::vector<phantomx::consensus::Vote> buffered_votes;
  std::vector<phantomx::consensus::Vote> deferred_votes;
  std::vector<phantomx::consensus::QuorumCertificate> deferred_qcs;
  std::vector<phantomx::consensus::FinalityCertificate> deferred_finality;
  std::vector<phantomx::consensus::NewView> own_new_views;
  std::mutex proposer_mu;
  std::array<std::uint8_t, 32> active_proposer{};
  bool proposer_override = false;
  std::uint64_t override_epoch = 0;
  std::uint64_t override_view = 0;
  std::unordered_set<std::string> validated_proposals;
  std::atomic<bool> pending_stop{false};
  std::string bft_wal_path = (datadir / "bft_wal.bin").string();

  if (bft_enabled) {
    auto vs_r = phantomx::consensus::build_validator_set(bft_cfg, 0);
    if (!vs_r.ok()) {
      logger.error("bft_validator_set_invalid", {{"error", vs_r.error().to_string()}});
      std::cerr << "invalid BFT validator set: " << vs_r.error().to_string() << "\n";
      return 2;
    }
    auto validators = std::move(vs_r.value());
    // Sanity: local key must be in validator set for BFT participation.
    const auto* self = phantomx::consensus::find_validator(validators, local_keypair.pub.bytes);
    if (self == nullptr) {
      logger.error("bft_local_identity_not_validator", {{"reason", "local signing key is not a configured validator"}});
      std::cerr << "local BFT signing key is not a configured validator\n";
      return 2;
    }
    logger.info("bft_validator_set_loaded", {{"validator_count", std::to_string(validators.validators.size())}, {"total_voting_power", std::to_string(validators.total_voting_power)}, {"key_mode", bft_cfg.key_mode == phantomx::consensus::BftKeyMode::kOperator ? "operator" : "seed"}});
    bft = std::make_unique<phantomx::consensus::BftEngine>(validators);
    auto lr = bft->load_state(bft_wal_path);
    if (lr.ok()) logger.info("bft_wal_loaded", {{"path", bft_wal_path}});
    else logger.warn("bft_wal_load_failed", {{"path", bft_wal_path}, {"error", lr.error().to_string()}});
  }

  auto elected_proposer = [&](std::uint64_t epoch, std::uint64_t view) {
    {
      std::scoped_lock pm(proposer_mu);
      if (proposer_override && epoch == override_epoch && view == override_view) return active_proposer;
    }
    std::scoped_lock lk(engine_mu);
    if (!bft) return std::array<std::uint8_t, 32>{};
    return phantomx::consensus::select_proposer(bft->validators(), epoch, view);
  };
  auto save_bft_wal = [&]() {
    if (!bft) return;
    auto r = bft->save_state(bft_wal_path);
    if (!r.ok()) logger.warn("bft_wal_save_failed", {{"error", r.error().to_string()}});
  };
  std::unordered_set<std::string> seen_qc_broadcasts;
  std::unordered_set<std::string> seen_local_votes;
  auto canonical_qc_key = [](const phantomx::consensus::QuorumCertificate& qc) {
    std::string qckey = std::to_string(static_cast<int>(qc.phase)) + ":" + std::to_string(qc.epoch) + ":" +
                        std::to_string(qc.height) + ":" + std::to_string(qc.view) + ":" +
                        phantomx::to_hex(std::span<const std::uint8_t>(qc.block_hash.data(), qc.block_hash.size()));
    std::vector<std::string> voters;
    voters.reserve(qc.votes.size());
    for (const auto& v : qc.votes) voters.push_back(phantomx::to_hex(std::span<const std::uint8_t>(v.validator_pubkey.data(), v.validator_pubkey.size())));
    std::sort(voters.begin(), voters.end());
    for (const auto& who : voters) qckey += ":" + who;
    return qckey;
  };
  auto proposal_key = [](std::uint64_t epoch, std::uint64_t view, std::uint32_t height,
                         const phantomx::Hash256Bytes& hash) {
    return std::to_string(epoch) + ":" + std::to_string(view) + ":" + std::to_string(height) + ":" +
           phantomx::to_hex(std::span<const std::uint8_t>(hash.data(), hash.size()));
  };
  // Callers hold engine_mu. pending_mu is taken only inside the defer helpers.
  auto is_proposal_validated = [&](std::uint64_t epoch, std::uint64_t view, std::uint32_t height,
                                   const phantomx::Hash256Bytes& hash) {
    return validated_proposals.count(proposal_key(epoch, view, height, hash)) != 0;
  };
  auto defer_vote = [&](const phantomx::consensus::Vote& v) {
    std::scoped_lock ql(pending_mu);
    const bool dup = std::any_of(deferred_votes.begin(), deferred_votes.end(), [&](const auto& x) {
      return x.validator_pubkey == v.validator_pubkey && x.phase == v.phase &&
             x.epoch == v.epoch && x.height == v.height && x.view == v.view && x.block_hash == v.block_hash;
    });
    if (!dup) deferred_votes.push_back(v);
  };
  auto defer_qc = [&](const phantomx::consensus::QuorumCertificate& qc) {
    std::scoped_lock ql(pending_mu);
    const auto qckey = canonical_qc_key(qc);
    const bool dup = std::any_of(deferred_qcs.begin(), deferred_qcs.end(), [&](const auto& x) { return canonical_qc_key(x) == qckey; });
    if (!dup) deferred_qcs.push_back(qc);
    return !dup;
  };
  auto defer_finality = [&](const phantomx::consensus::FinalityCertificate& f) {
    std::scoped_lock ql(pending_mu);
    const bool dup = std::any_of(deferred_finality.begin(), deferred_finality.end(), [&](const auto& x) {
      return x.height == f.height && x.block_hash == f.block_hash;
    });
    if (!dup) deferred_finality.push_back(f);
    return !dup;
  };
  std::atomic<std::uint64_t> last_progress_ms{0};
  auto now_ms = []() -> std::uint64_t {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
  };
  last_progress_ms.store(now_ms());
  auto view_timeout_ms = [&]() -> std::uint64_t {
    std::scoped_lock lk(engine_mu);
    const auto view = bft->state().view;
    std::uint64_t t = 4000;
    for (std::uint64_t i = 0; i < view && t < 30000; ++i) t = std::min<std::uint64_t>(30000, (t * 3) / 2);
    return t;
  };
  auto mark_progress = [&](const char* reason = "progress") {
    const auto elapsed = now_ms() - last_progress_ms.load();
    const auto limit = view_timeout_ms();
    if (elapsed >= limit / 2) {
      logger.info("bft_view_timer_cancelled", {{"stage", "view_timer_cancelled"}, {"reason", reason}, {"elapsed_ms", std::to_string(elapsed)}, {"timeout_ms", std::to_string(limit)}});
    }
    last_progress_ms.store(now_ms());
    logger.info("bft_view_timer_scheduled", {{"stage", "view_timer_scheduled"}, {"reason", reason}, {"timeout_ms", std::to_string(view_timeout_ms())}});
  };
  phantomx::rpc::RpcServer rpc;
  phantomx::rpc::register_default_methods(rpc);
  rpc.add_user({"admin", rpc_token, {}});
  rpc.set_rate_limit(1000, 60);
  auto registered = phantomx::rpc::register_node_rpc_handlers(rpc, chain, mempool, pool, aggregator, "", "", nullptr, bft.get(), &key.value(), nullptr, elected_proposer, network);
  if (!registered.ok()) { logger.error("rpc_registration_failed", {{"error", registered.error().to_string()}}); return 1; }

  phantomx::network::NetworkConfig p2p_network;
  p2p_network.listen_host = p2p_host;
  p2p_network.listen_port = static_cast<std::uint16_t>(std::stoul(p2p_port_s));
  const auto connect_it = args.options.find("--connect");
  if (connect_it != args.options.end() && !connect_it->second.empty()) {
    // Comma-separated seeds: regtest needs a full mesh so a crashed leaf does
    // not partition the remaining quorum.
    std::stringstream seeds(connect_it->second);
    std::string target;
    while (std::getline(seeds, target, ',')) {
      if (target.empty()) continue;
      const auto sep = target.rfind(':');
      if (sep == std::string::npos) { std::cerr << "invalid --connect host:port\n"; return 2; }
      p2p_network.seed_peers.push_back({target.substr(0, sep), static_cast<std::uint16_t>(std::stoul(target.substr(sep + 1)))});
    }
  }
  // Wire canonical network identity into P2P. Previously these stayed at
  // NetworkConfig defaults (regtest magic 0x52505801, zero genesis) for every
  // network, which made the handshake identity gate vacuous and allowed
  // cross-network peering. Use the authoritative per-network definitions only.
  p2p_network.network_magic = phantomx::network_magic(network);
  p2p_network.genesis_hash = phantomx::block_hash(phantomx::genesis_block(network).header);
  if (p2p_network.network_magic == 0) {
    std::cerr << "invalid/missing network magic for selected network\n";
    return 2;
  }
  {
    bool zero_genesis = true;
    for (auto b : p2p_network.genesis_hash) { if (b != 0) { zero_genesis = false; break; } }
    if (zero_genesis) {
      std::cerr << "invalid/missing genesis hash for selected network\n";
      return 2;
    }
  }
  std::clog << "{\"event\":\"p2p_network_identity\",\"network\":\"" << phantomx::network_name(network)
            << "\",\"magic\":\"0x" << std::hex << p2p_network.network_magic << std::dec
            << "\",\"genesis\":\"" << phantomx::to_hex(std::span<const std::uint8_t>(p2p_network.genesis_hash.data(), p2p_network.genesis_hash.size())) << "\"}" << std::endl;

  phantomx::network::P2PNode p2p(p2p_network, &chain, &mempool);

  // Wire the live consensus engine to the network. Each callback validates before
  // advancing local state; finalized blocks are applied through ChainDatabase.
  auto submit_local_vote = [&](phantomx::consensus::VotePhase phase,
                               const phantomx::consensus::BlockProposal& proposal) {
    const auto phase_name = phase == phantomx::consensus::VotePhase::kPrevote ? "prevote" : "precommit";
    const auto vote_key = std::to_string(static_cast<int>(phase)) + ":" + std::to_string(proposal.epoch) + ":" +
                          std::to_string(proposal.height) + ":" + std::to_string(proposal.view) + ":" +
                          phantomx::to_hex(std::span<const std::uint8_t>(proposal.block_hash.data(), proposal.block_hash.size()));
    if (!seen_local_votes.insert(vote_key).second) return;
    const auto* self = phantomx::consensus::find_validator(bft->validators(), key.value().pub.bytes);
    if (self == nullptr || !self->active) { logger.warn("bft_vote_rejected", {{"stage", phase_name}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", "validator_inactive"}}); return; }
    phantomx::consensus::Vote vote;
    vote.phase = phase; vote.epoch = proposal.epoch; vote.view = proposal.view;
    vote.height = proposal.height; vote.block_hash = proposal.block_hash;
    vote.validator_pubkey = key.value().pub.bytes; vote.voting_power = self->voting_power;
    auto signed_vote = phantomx::consensus::sign_vote(vote, key.value().sec);
    if (!signed_vote.ok()) { logger.warn("bft_vote_rejected", {{"stage", phase_name}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", "sign_failed"}}); return; }
    auto submitted = bft->submit_vote(signed_vote.value());
    if (!submitted.ok()) { logger.warn("bft_vote_rejected", {{"stage", phase_name}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", submitted.error().to_string()}}); return; }
    save_bft_wal();
    auto sent = p2p.broadcast_bft_vote(signed_vote.value());
    logger.info("bft_vote_broadcast", {{"stage", phase_name}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"sent", sent.ok() ? "true" : "false"}, {"error", sent.ok() ? "" : sent.error().to_string()}});
  };
  auto apply_finality = [&](const phantomx::consensus::FinalityCertificate& finality, bool broadcast = true) {
    if (bft->state().last_finalized.has_value() && finality.height <= bft->state().last_finalized->height) {
      logger.info("bft_finality_ignored", {{"reason", "already_finalized"},
                                           {"height", std::to_string(finality.height)},
                                           {"finalized_height", std::to_string(bft->state().last_finalized->height)}});
      return;
    }
    if (broadcast) {
      auto sent = p2p.broadcast_bft_finality(finality);
      logger.info("bft_precommit_broadcast", {{"stage", "precommit_broadcast"}, {"height", std::to_string(finality.height)}, {"view", std::to_string(finality.precommit_qc.view)}, {"sent", sent.ok() ? "true" : "false"}, {"error", sent.ok() ? "" : sent.error().to_string()}});
    }
    if (!bft->finalize(finality).ok()) return;
    auto block = chain.read_block(finality.block_hash);
    if (block.ok()) {
      auto index = chain.get_block_index(finality.block_hash);
      if (index.ok() && (!index.value().has_value() || !index.value()->active))
        (void)chain.apply_block(block.value());
    }
    (void)chain.set_metadata("finalized_height", std::to_string(finality.height));
    (void)chain.set_metadata("finalized_hash", phantomx::to_hex(finality.block_hash));
    logger.info("bft_commit_finality_applied", {{"stage", "commit_finality_applied"}, {"height", std::to_string(finality.height)}, {"finalized_height", std::to_string(bft->state().last_finalized->height)}});
    save_bft_wal();
    mark_progress();
  };
  auto process_qc = [&](const phantomx::consensus::QuorumCertificate& qc) {
    // A QC proves votes but not that this node validated the block. Replay it
    // after the matching proposal instead of finalizing an unseen height.
    if (!is_proposal_validated(qc.epoch, qc.view, qc.height, qc.block_hash)) {
      const bool novel = defer_qc(qc);
      if (novel) logger.info("bft_qc_deferred", {{"stage", "qc_deferred"}, {"phase", std::to_string(static_cast<int>(qc.phase))}, {"height", std::to_string(qc.height)}, {"view", std::to_string(qc.view)}});
      return;
    }
    if (!phantomx::consensus::verify_qc(bft->validators(), qc)) {
      logger.warn("bft_qc_rejected", {{"stage", "qc_received"}, {"phase", std::to_string(static_cast<int>(qc.phase))}, {"height", std::to_string(qc.height)}, {"view", std::to_string(qc.view)}, {"reason", "invalid_qc"}});
      return;
    }
    logger.info("bft_qc_received", {{"stage", "qc_received"}, {"phase", std::to_string(static_cast<int>(qc.phase))}, {"height", std::to_string(qc.height)}, {"view", std::to_string(qc.view)}, {"vote_count", std::to_string(qc.votes.size())}, {"voting_power", std::to_string(qc.signed_voting_power)}});
    (void)bft->observe_qc(qc);
    // Duplicate/retransmitted votes can re-form the same QC; gossip each
    // distinct QC once and never let duplicates reset the view timer.
    const bool novel = seen_qc_broadcasts.insert(canonical_qc_key(qc)).second;
    if (novel) {
      auto sent = p2p.broadcast_bft_qc(qc);
      logger.info("bft_qc_broadcast", {{"stage", "qc_broadcast"}, {"phase", std::to_string(static_cast<int>(qc.phase))}, {"height", std::to_string(qc.height)}, {"view", std::to_string(qc.view)}, {"sent", sent.ok() ? "true" : "false"}, {"error", sent.ok() ? "" : sent.error().to_string()}});
    }
    save_bft_wal();
    if (qc.phase == phantomx::consensus::VotePhase::kPrevote) {
      phantomx::consensus::BlockProposal proposal;
      proposal.epoch = qc.epoch; proposal.view = qc.view; proposal.height = qc.height; proposal.block_hash = qc.block_hash;
      submit_local_vote(phantomx::consensus::VotePhase::kPrecommit, proposal);
      if (novel) mark_progress();
      return;
    }
    if (qc.phase != phantomx::consensus::VotePhase::kPrecommit) return;
    phantomx::consensus::FinalityCertificate finality;
    finality.height = qc.height; finality.block_hash = qc.block_hash;
    finality.precommit_qc = qc;
    apply_finality(finality);
  };
  // Called with engine_mu held, after a proposal has been validated and its key
  // inserted into validated_proposals. Earlier votes/QCs/finality are safe now.
  auto replay_deferred = [&]() {
    std::vector<phantomx::consensus::Vote> votes;
    std::vector<phantomx::consensus::QuorumCertificate> qcs;
    std::vector<phantomx::consensus::FinalityCertificate> finals;
    {
      std::scoped_lock ql(pending_mu);
      auto vi = std::remove_if(deferred_votes.begin(), deferred_votes.end(), [&](const auto& v) {
        if (!is_proposal_validated(v.epoch, v.view, v.height, v.block_hash)) return false;
        votes.push_back(v); return true;
      });
      deferred_votes.erase(vi, deferred_votes.end());
      auto qi = std::remove_if(deferred_qcs.begin(), deferred_qcs.end(), [&](const auto& qc) {
        if (!is_proposal_validated(qc.epoch, qc.view, qc.height, qc.block_hash)) return false;
        qcs.push_back(qc); return true;
      });
      deferred_qcs.erase(qi, deferred_qcs.end());
      auto fi = std::remove_if(deferred_finality.begin(), deferred_finality.end(), [&](const auto& f) {
        const auto& qc = f.precommit_qc;
        if (!is_proposal_validated(qc.epoch, qc.view, f.height, f.block_hash)) return false;
        finals.push_back(f); return true;
      });
      deferred_finality.erase(fi, deferred_finality.end());
    }
    if (votes.empty() && qcs.empty() && finals.empty()) return;
    logger.info("bft_deferred_replay", {{"stage", "deferred_replay"}, {"votes", std::to_string(votes.size())}, {"qcs", std::to_string(qcs.size())}, {"finality", std::to_string(finals.size())}});
    for (const auto& v : votes) {
      const auto accepted = bft->submit_vote(v);
      if (!accepted.ok()) continue;
      save_bft_wal();
      auto qc = bft->maybe_qc(v.phase, v.epoch, v.view, v.block_hash);
      if (qc.has_value()) process_qc(*qc);
    }
    for (const auto& qc : qcs) process_qc(qc);
    for (const auto& f : finals) apply_finality(f, false);
  };
  auto handle_proposal = [&](const phantomx::consensus::BlockProposal& proposal) {
    std::scoped_lock lk(engine_mu);
    logger.info("bft_proposal_received", {{"stage", "proposal_received"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}});
    bool proposer_ok = phantomx::consensus::verify_proposal(bft->validators(), proposal);
    if (!proposer_ok) {
      std::array<std::uint8_t, 32> elected{};
      bool has_override = false;
      {
        std::scoped_lock pm(proposer_mu);
        if (proposer_override && proposal.epoch == override_epoch && proposal.view == override_view) {
          elected = active_proposer;
          has_override = true;
        }
      }
      if (has_override) {
        const auto* validator = phantomx::consensus::find_validator(bft->validators(), elected);
        if (validator != nullptr && validator->active) {
          phantomx::consensus::ValidatorSet one;
          one.epoch = proposal.epoch;
          one.validators.push_back(*validator);
          one.total_voting_power = validator->voting_power;
          proposer_ok = phantomx::consensus::verify_proposal(one, proposal);
        }
      }
    }
    if (!proposer_ok) {
      logger.warn("bft_proposal_rejected", {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", "invalid_signature"}});
      return;
    }
    if (proposal.epoch != bft->state().epoch) {
      logger.warn("bft_proposal_rejected", {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", "epoch_mismatch"}});
      return;
    }
    if (proposal.height == bft->state().height && proposal.view > bft->state().view && proposal.view <= bft->state().view + 5) {
      const bool dup = std::any_of(buffered_proposals.begin(), buffered_proposals.end(), [&](const auto& x) {
        return x.block_hash == proposal.block_hash && x.view == proposal.view && x.height == proposal.height;
      });
      const bool dropped = !dup && buffered_proposals.size() >= 64;
      if (!dup && !dropped) buffered_proposals.push_back(proposal);
      logger.info("bft_vote_buffered", {{"stage", "proposal_buffered"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"dedup", dup ? "true" : "false"}, {"dropped", dropped ? "true" : "false"}});
      return;
    }
    if (proposal.view != bft->state().view) {
      logger.warn("bft_proposal_rejected", {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", "stale_view"}});
      return;
    }
    logger.info("bft_proposal_validated", {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}});
    auto tip = chain.tip();
    if (!tip.ok()) { logger.warn("bft_proposal_rejected", {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", "tip_read_failed"}}); return; }
    if (tip.value().has_value() && proposal.parent_hash != tip.value()->hash) { logger.warn("bft_proposal_rejected", {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", "bad_prev_hash"}}); return; }
    if (tip.value().has_value() && proposal.height != tip.value()->height + 1) { logger.warn("bft_proposal_rejected", {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", "bad_height"}}); return; }
    if (!tip.value().has_value() && proposal.height != 0) { logger.warn("bft_proposal_rejected", {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", "bad_height"}}); return; }
    auto block = chain.read_block(proposal.block_hash);
    if (!block.ok()) {
      std::scoped_lock ql(pending_mu);
      const bool exists = std::any_of(pending_proposals.begin(), pending_proposals.end(), [&](const auto& p) { return p.block_hash == proposal.block_hash && p.view == proposal.view && p.height == proposal.height; });
      if (!exists) pending_proposals.push_back(proposal);
      pending_cv.notify_one();
      logger.info("bft_proposal_deferred", {{"stage", "proposal_deferred"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", "missing_block"}});
      return;
    }
    if (phantomx::block_hash(block.value().header) != proposal.block_hash) { logger.warn("bft_proposal_rejected", {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", "block_hash_mismatch"}}); return; }
    if (block.value().header.height != proposal.height || block.value().header.prev_hash != proposal.parent_hash) { logger.warn("bft_proposal_rejected", {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", "block_header_mismatch"}}); return; }
    auto block_valid = chain.validate_block(block.value());
    if (!block_valid.ok()) { logger.warn("bft_proposal_rejected", {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", block_valid.error().to_string()}}); return; }
    auto lock_check = bft->check_proposal_lock(proposal);
    if (!lock_check.ok()) {
      logger.warn("bft_proposal_rejected", {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"reason", lock_check.error().to_string()}});
      return;
    }
    if (bft->state().locked_hash.has_value() && bft->state().locked_hash.value() != proposal.block_hash) {
      const bool extends_lock = bft->state().locked_height + 1 == proposal.height &&
                                proposal.parent_hash == bft->state().locked_hash.value();
      logger.info(extends_lock ? "bft_lock_extended" : "bft_lock_unlocked_by_justify_qc",
                  {{"stage", "proposal_validated"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}});
    }
    validated_proposals.insert(proposal_key(proposal.epoch, proposal.view, proposal.height, proposal.block_hash));
    logger.info("bft_proposal_registered", {{"stage", "proposal_registered"}, {"height", std::to_string(proposal.height)}, {"view", std::to_string(proposal.view)}, {"block_hash", phantomx::to_hex(std::span<const std::uint8_t>(proposal.block_hash.data(), proposal.block_hash.size()))}});
    submit_local_vote(phantomx::consensus::VotePhase::kPrevote, proposal);
    replay_deferred();
    mark_progress();
  };
  auto reprocess_buffered = [&]() {
    auto props = buffered_proposals;
    auto votes = buffered_votes;
    buffered_proposals.clear();
    buffered_votes.clear();
    for (const auto& p : props) handle_proposal(p);
    for (const auto& v : votes) {
      if (v.epoch == bft->state().epoch && v.view == bft->state().view) (void)bft->submit_vote(v);
    }
  };

  // A NewView is the only quorum-authentified reason to leave the current view.
  // The proposer signature and its ViewChangeQC are verified before state changes.
  auto process_new_view = [&](const phantomx::consensus::NewView& nv) {
    if (!view_change_enabled) return;
    if (!phantomx::consensus::verify_new_view(bft->validators(), nv)) {
      logger.warn("bft_new_view_rejected", {{"stage", "new_view_received"}, {"view", std::to_string(nv.view)}, {"reason", "invalid_signature_or_qc"}});
      return;
    }
    if (nv.epoch != bft->state().epoch || nv.height != bft->state().height) {
      logger.warn("bft_new_view_rejected", {{"stage", "new_view_received"}, {"view", std::to_string(nv.view)}, {"reason", "epoch_height_mismatch"}});
      return;
    }
    if (nv.view <= bft->state().view || nv.view > bft->state().view + kMaxViewJump) {
      logger.warn("bft_new_view_rejected", {{"stage", "new_view_received"}, {"view", std::to_string(nv.view)}, {"reason", "stale_or_distant_view"}});
      return;
    }
    const auto expected = phantomx::consensus::select_new_view_proposer(bft->validators(), nv.view_change_qc);
    if (nv.proposer_pubkey != expected) {
      logger.warn("bft_new_view_rejected", {{"stage", "new_view_received"}, {"view", std::to_string(nv.view)}, {"reason", "wrong_proposer"}});
      return;
    }
    const auto old_view = bft->state().view;
    auto entered = bft->enter_view(nv.view);
    if (!entered.ok()) {
      logger.warn("bft_new_view_rejected", {{"stage", "new_view_received"}});
      return;
    }
    {
      std::scoped_lock pm(proposer_mu);
      active_proposer = nv.proposer_pubkey;
      override_epoch = nv.epoch;
      override_view = nv.view;
      proposer_override = true;
    }
    logger.info("bft_view_changed", {{"stage", "view_changed"}, {"epoch", std::to_string(nv.epoch)}, {"old_view", std::to_string(old_view)}, {"new_view", std::to_string(bft->state().view)}, {"height", std::to_string(nv.height)}, {"reason", "new_view_quorum"}});
    own_new_views.erase(
        std::remove_if(own_new_views.begin(), own_new_views.end(), [&](const auto& old_nv) { return old_nv.view + 2 < bft->state().view; }),
        own_new_views.end());
    {
      std::scoped_lock ql(pending_mu);
      pending_proposals.clear();
      // Votes/QCs from the old view can never become valid after a view change.
      deferred_votes.clear();
      deferred_qcs.clear();
      deferred_finality.clear();
    }
    mark_progress();
    save_bft_wal();
    reprocess_buffered();
    if (nv.proposal.has_value()) handle_proposal(nv.proposal.value());
  };

  // Only the deterministically selected proposer emits NewView. Entering the view
  // (via process_new_view) also prevents duplicate emissions for the same QC.
  auto maybe_emit_new_view = [&](const phantomx::consensus::ViewChangeQC& qc) {
    if (!view_change_enabled || qc.new_view <= bft->state().view) return;
    const auto proposer = phantomx::consensus::select_new_view_proposer(bft->validators(), qc);
    logger.info("bft_new_view_decision", {{"stage", "new_view_decision"}, {"view", std::to_string(qc.new_view)}, {"is_proposer", key.value().pub.bytes == proposer ? "true" : "false"}, {"self", phantomx::to_hex(std::span<const std::uint8_t>(key.value().pub.bytes.data(), key.value().pub.bytes.size()))}, {"proposer", phantomx::to_hex(std::span<const std::uint8_t>(proposer.data(), proposer.size()))}, {"qc_votes", std::to_string(qc.votes.size())}});
    if (key.value().pub.bytes != proposer) return;
    phantomx::consensus::NewView nv;
    nv.epoch = qc.epoch; nv.height = qc.height; nv.view = qc.new_view;
    nv.view_change_qc = qc;
    nv.proposer_pubkey = proposer;
    auto signed_nv = phantomx::consensus::sign_new_view(nv, key.value().sec);
    if (!signed_nv.ok()) {
      logger.warn("bft_new_view_rejected", {{"stage", "new_view_build"}, {"view", std::to_string(qc.new_view)}, {"reason", "sign_failed"}});
      return;
    }
    auto sent = p2p.broadcast_bft_new_view(signed_nv.value());
    logger.info("bft_new_view_broadcast", {{"stage", "new_view_broadcast"}, {"view", std::to_string(qc.new_view)}, {"height", std::to_string(qc.height)}, {"sent", sent.ok() ? "true" : "false"}, {"error", sent.ok() ? "" : sent.error().to_string()}});
    own_new_views.push_back(signed_nv.value());
    process_new_view(signed_nv.value());
  };

  // BFT gossip ingress: every callback validates before mutating local state.
  // engine_mu is recursive, so nested replay/reprocess paths are safe.
  if (bft_enabled) {
    p2p.set_bft_proposal_callback([&](const phantomx::consensus::BlockProposal& proposal) {
      handle_proposal(proposal);
    });
    p2p.set_bft_vote_callback([&](const phantomx::consensus::Vote& vote) {
      std::scoped_lock lk(engine_mu);
      if (vote.epoch == bft->state().epoch && vote.height == bft->state().height &&
          vote.view > bft->state().view && vote.view <= bft->state().view + 5) {
        const bool dup = std::any_of(buffered_votes.begin(), buffered_votes.end(), [&](const auto& x) {
          return x.validator_pubkey == vote.validator_pubkey && x.phase == vote.phase &&
                 x.view == vote.view && x.height == vote.height && x.block_hash == vote.block_hash;
        });
        const bool dropped = !dup && buffered_votes.size() >= 256;
        if (!dup && !dropped) buffered_votes.push_back(vote);
        logger.info("bft_vote_buffered", {{"stage", "vote_buffered"}, {"height", std::to_string(vote.height)}, {"view", std::to_string(vote.view)}, {"phase", vote.phase == phantomx::consensus::VotePhase::kPrevote ? "prevote" : "precommit"}, {"dedup", dup ? "true" : "false"}, {"dropped", dropped ? "true" : "false"}});
        return;
      }
      auto received = bft->submit_vote(vote);
      const auto phase_name = vote.phase == phantomx::consensus::VotePhase::kPrevote ? "prevote" : "precommit";
      if (!received.ok()) {
        logger.warn("bft_vote_rejected", {{"stage", phase_name + std::string("_received")}, {"height", std::to_string(vote.height)}, {"view", std::to_string(vote.view)}, {"reason", received.error().to_string()}});
        return;
      }
      save_bft_wal();
      auto qc = bft->maybe_qc(vote.phase, vote.epoch, vote.view, vote.block_hash);
      logger.info("bft_qc_evaluated", {{"stage", phase_name + std::string("_qc_evaluated")}, {"height", std::to_string(vote.height)}, {"view", std::to_string(vote.view)}, {"formed", qc.has_value() ? "true" : "false"}});
      if (qc.has_value()) process_qc(*qc);
    });
    p2p.set_bft_qc_callback([&](const phantomx::consensus::QuorumCertificate& qc) {
      std::scoped_lock lk(engine_mu);
      process_qc(qc);
    });
    p2p.set_bft_finality_callback([&](const phantomx::consensus::FinalityCertificate& finality) {
      std::scoped_lock lk(engine_mu);
      apply_finality(finality, false);
    });
    p2p.set_bft_view_change_callback([&](const phantomx::consensus::ViewChange& vc) {
      std::scoped_lock lk(engine_mu);
      auto r = bft->submit_view_change(vc);
      if (!r.ok()) {
        logger.warn("bft_view_change_rejected", {{"stage", "view_change_received"}, {"epoch", std::to_string(vc.epoch)}, {"height", std::to_string(vc.height)}, {"new_view", std::to_string(vc.new_view)}, {"reason", r.error().to_string()}});
        return;
      }
      logger.info("bft_view_change_received", {{"stage", "view_change_received"}, {"epoch", std::to_string(vc.epoch)}, {"height", std::to_string(vc.height)}, {"new_view", std::to_string(vc.new_view)}});
      save_bft_wal();
      auto qc = bft->maybe_view_change_qc();
      if (qc.has_value()) maybe_emit_new_view(*qc);
    });
    p2p.set_bft_new_view_callback([&](const phantomx::consensus::NewView& nv) {
      std::scoped_lock lk(engine_mu);
      process_new_view(nv);
    });
  }

  auto started = p2p.start();
  if (!started.ok()) { logger.error("p2p_start_failed", {{"error", started.error().to_string()}}); return 1; }
  // Replace handlers with the live P2P pointer before exposing RPC.
  rpc = phantomx::rpc::RpcServer{};
  rpc.register_method("getready", [](const phantomx::rpc::RpcRequest& req) { return phantomx::rpc::RpcResponse{req.id, true, phantomx::rpc::RpcErrorCode::kOk, "", {{"ready", "true"}}}; });
  phantomx::rpc::register_default_methods(rpc);
  rpc.add_user({"admin", rpc_token, {}});
  rpc.set_rate_limit(1000, 60);
  registered = phantomx::rpc::register_node_rpc_handlers(rpc, chain, mempool, pool, aggregator, "", "", &p2p, bft.get(), &key.value(), handle_proposal, elected_proposer, network);
  if (!registered.ok()) return 1;
  std::thread pending_worker([&]() {
    if (!bft) {
      while (!pending_stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      return;
    }
    auto last_retransmit = std::chrono::steady_clock::now();
    while (!pending_stop.load()) {
      std::vector<phantomx::consensus::BlockProposal> batch;
      {
        std::unique_lock lk(pending_mu);
        pending_cv.wait_for(lk, std::chrono::milliseconds(100), [&]() { return pending_stop.load() || !pending_proposals.empty(); });
        batch.swap(pending_proposals);
      }
      for (const auto& proposal : batch) handle_proposal(proposal);

      const auto now = std::chrono::steady_clock::now();
      if (now - last_retransmit >= std::chrono::seconds(2)) {
        last_retransmit = now;
        std::vector<phantomx::consensus::Vote> own_votes;
        {
          std::scoped_lock lk(engine_mu);
          for (const auto& [_, v] : bft->votes()) {
            if (v.validator_pubkey == key.value().pub.bytes &&
                v.epoch == bft->state().epoch && v.view == bft->state().view) {
              own_votes.push_back(v);
            }
          }
        }
        for (const auto& v : own_votes) {
          auto sent = p2p.broadcast_bft_vote(v);
          logger.info("bft_vote_retransmit", {{"stage", "retransmit"}, {"height", std::to_string(v.height)}, {"view", std::to_string(v.view)}, {"phase", std::to_string(static_cast<int>(v.phase))}, {"sent", sent.ok() ? "true" : "false"}, {"error", sent.ok() ? "" : sent.error().to_string()}});
        }
        if (view_change_enabled) {
          std::vector<phantomx::consensus::ViewChange> own_view_changes;
          {
            std::scoped_lock lk(engine_mu);
            for (const auto& [_, vc] : bft->view_changes()) {
              if (vc.validator_pubkey == key.value().pub.bytes &&
                  vc.epoch == bft->state().epoch && vc.height == bft->state().height &&
                  vc.new_view > bft->state().view) {
                own_view_changes.push_back(vc);
              }
            }
          }
          for (const auto& vc : own_view_changes) {
            auto sent = p2p.broadcast_bft_view_change(vc);
            logger.info("bft_view_change_retransmit", {{"stage", "retransmit"}, {"height", std::to_string(vc.height)}, {"new_view", std::to_string(vc.new_view)}, {"sent", sent.ok() ? "true" : "false"}, {"error", sent.ok() ? "" : sent.error().to_string()}});
          }
          std::vector<phantomx::consensus::NewView> own_nvs;
          {
            std::scoped_lock lk(engine_mu);
            for (const auto& nv : own_new_views) {
              if (nv.epoch == bft->state().epoch && nv.height == bft->state().height && nv.view == bft->state().view)
                own_nvs.push_back(nv);
            }
          }
          for (const auto& nv : own_nvs) {
            auto sent = p2p.broadcast_bft_new_view(nv);
            logger.info("bft_new_view_retransmit", {{"stage", "retransmit"}, {"view", std::to_string(nv.view)}, {"height", std::to_string(nv.height)}, {"sent", sent.ok() ? "true" : "false"}, {"error", sent.ok() ? "" : sent.error().to_string()}});
          }
        }
      }

      const auto elapsed = now_ms() - last_progress_ms.load();
      const auto timeout_limit = view_timeout_ms();
      if (elapsed >= timeout_limit) {
        std::scoped_lock lk(engine_mu);
        const auto old_view = bft->state().view;
        const auto epoch = bft->state().epoch;
        const auto height = bft->state().height;
        const auto new_view = old_view + 1;
        logger.warn("bft_view_timer_fired", {{"stage", "view_timer_fired"}, {"epoch", std::to_string(epoch)}, {"view", std::to_string(old_view)}, {"height", std::to_string(height)}, {"elapsed_ms", std::to_string(elapsed)}, {"timeout_ms", std::to_string(timeout_limit)}});
        const auto* self = phantomx::consensus::find_validator(bft->validators(), key.value().pub.bytes);
        if (view_change_enabled) {
          // Quorum-gated pacemaker: emit VC, then wait for a signed NewView from
          // the elected proposer. A lone timeout must never advance local view.
          if (self != nullptr && self->active) {
            phantomx::consensus::ViewChange vc;
            vc.epoch = epoch; vc.height = height; vc.new_view = new_view;
            vc.validator_pubkey = key.value().pub.bytes; vc.voting_power = self->voting_power;
            vc.high_qc = bft->state().valid_qc;
            auto svc = phantomx::consensus::sign_view_change(vc, key.value().sec);
            if (svc.ok()) {
              auto submitted = bft->submit_view_change(svc.value());
              auto sent = p2p.broadcast_bft_view_change(svc.value());
              logger.info("bft_view_change_broadcast", {{"stage", "view_change_broadcast"}, {"sent", sent.ok() ? "true" : "false"}, {"error", sent.ok() ? "" : sent.error().to_string()}});
              save_bft_wal();
            }
          }
          auto qc = bft->maybe_view_change_qc();
          if (qc.has_value()) maybe_emit_new_view(qc.value());
        } else {
          // Legacy local-timeout baseline retained only when the feature is off.
          { std::scoped_lock pm(proposer_mu); proposer_override = false; }
          auto tr = bft->timeout(now_ms());
          if (tr.ok()) {
            logger.info("bft_view_changed", {{"stage", "view_changed"}, {"epoch", std::to_string(epoch)}, {"old_view", std::to_string(old_view)}, {"new_view", std::to_string(bft->state().view)}, {"height", std::to_string(height)}, {"reason", "local_timeout"}});
            { std::scoped_lock ql(pending_mu); pending_proposals.clear(); }
            reprocess_buffered();
            save_bft_wal();
          } else {
            logger.warn("bft_view_change_failed", {{"stage", "view_change_failed"}});
          }
        }
        last_progress_ms.store(now_ms());
      }
    }
  });
  std::thread server([&]() {
    auto result = phantomx::rpc::serve_rpc(rpc, rpc_host, static_cast<std::uint16_t>(std::stoul(rpc_port_s)), rpc_allow_ips);
    if (!result.ok()) logger.error("rpc_server_failed", {{"error", result.error().to_string()}});
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  logger.info("ready", {{"mode", std::string(phantomx::network_name(network))}, {"rpc", rpc_host + ":" + rpc_port_s}, {"p2p", p2p_network.listen_host + ":" + std::to_string(p2p_network.listen_port)}, {"datadir", datadir.string()}});
  server.join();
  pending_stop.store(true);
  pending_cv.notify_all();
  if (pending_worker.joinable()) pending_worker.join();
  p2p.stop();
  return 0;
}
