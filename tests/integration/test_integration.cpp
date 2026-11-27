#include <filesystem>
#include <fstream>

#include "../test_framework.hpp"
#include "phantomx/common/cli.hpp"
#include "phantomx/common/config.hpp"
#include "phantomx/common/serialization.hpp"
#include "phantomx/common/types.hpp"

PHX_TEST(integration_config_cli_serialization_flow) {
  const auto path = std::filesystem::temp_directory_path() / "phantomx-integration.cfg";
  {
    std::ofstream out(path);
    out << "address_prefix=PhantomX\n";
    out << "reward_atoms=2500000000\n";
  }

  auto cfg = phantomx::Config::load_file(path);
  PHX_EXPECT(cfg.ok());
  PHX_EXPECT(cfg.value().get_or("address_prefix", "") == std::string(phantomx::kAddressPrefix));

  phantomx::CliParser parser("phantomx-cli");
  char arg0[] = "phantomx-cli";
  char arg1[] = "status";
  char arg2[] = "--config";
  std::string path_str = path.string();
  std::vector<char> arg2v(path_str.begin(), path_str.end());
  arg2v.push_back('\0');
  char* argv[] = {arg0, arg1, arg2, arg2v.data()};
  auto parsed = parser.parse(4, argv);
  PHX_EXPECT(parsed.ok());
  PHX_EXPECT(parsed.value().command == "status");

  phantomx::BinaryWriter w;
  w.write_string(cfg.value().get_or("address_prefix", ""));
  w.write_u64(phantomx::kInitialBlockRewardAtoms);
  phantomx::BinaryReader r(w.buffer());
  auto prefix = r.read_string();
  auto reward = r.read_u64();
  PHX_EXPECT(prefix.ok() && prefix.value() == "PhantomX");
  PHX_EXPECT(reward.ok() && reward.value() == 2'500'000'000ULL);

  std::filesystem::remove(path);
}

int main() { return phantomx::test::run_all(); }
