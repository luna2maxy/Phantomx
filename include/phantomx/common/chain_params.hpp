#pragma once

#include <cstdint>
#include <string_view>

#include "phantomx/blockchain/genesis.hpp"
#include "phantomx/common/types.hpp"

namespace phantomx {

// Chain parameters are consensus/network-critical. Changes require versioning, migration, and tests.
struct ChainParams {
  Network network = Network::kMainnet;
  std::string_view name;
  std::uint32_t magic = 0;
  std::string_view address_hrp;
  std::uint16_t default_p2p_port = 0;
  std::uint16_t default_rpc_port = 0;
  PhxAtom initial_block_reward_atoms = kInitialBlockRewardAtoms;
  std::uint64_t halving_interval_blocks = kHalvingIntervalBlocks;
  std::uint64_t target_block_time_seconds = kTargetBlockTimeSeconds;
  PhxAtom max_supply_atoms = kTheoreticalMaxSupplyAtoms;
};

[[nodiscard]] inline ChainParams mainnet_params() {
  return {Network::kMainnet, "mainnet", network_magic(Network::kMainnet), address_hrp(Network::kMainnet),
          8333, 8740, kInitialBlockRewardAtoms, kHalvingIntervalBlocks, kTargetBlockTimeSeconds, kTheoreticalMaxSupplyAtoms};
}

[[nodiscard]] inline ChainParams testnet_params() {
  return {Network::kTestnet, "testnet", network_magic(Network::kTestnet), address_hrp(Network::kTestnet),
          18333, 18740, kInitialBlockRewardAtoms, kHalvingIntervalBlocks, kTargetBlockTimeSeconds, kTheoreticalMaxSupplyAtoms};
}

[[nodiscard]] inline ChainParams regtest_params() {
  return {Network::kRegtest, "regtest", network_magic(Network::kRegtest), address_hrp(Network::kRegtest),
          18444, 8741, kInitialBlockRewardAtoms, kHalvingIntervalBlocks, kTargetBlockTimeSeconds, kTheoreticalMaxSupplyAtoms};
}

[[nodiscard]] inline ChainParams params_for(Network n) {
  if (n == Network::kTestnet) return testnet_params();
  if (n == Network::kRegtest) return regtest_params();
  return mainnet_params();
}

}  // namespace phantomx
