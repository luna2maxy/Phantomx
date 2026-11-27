#include <filesystem>
#include <fstream>

#include "../test_framework.hpp"
#include "phantomx/common/config.hpp"

PHX_TEST(config_loads_key_values) {
  const auto path = std::filesystem::temp_directory_path() / "phantomx-test-config.cfg";
  {
    std::ofstream out(path);
    out << "# comment\n";
    out << "network=mainnet\n";
    out << "rpc_port = 8740 \n";
    out << "[section]\n";
    out << "log_level=debug\n";
  }
  auto cfg = phantomx::Config::load_file(path);
  PHX_EXPECT(cfg.ok());
  PHX_EXPECT(cfg.value().get_or("network", "") == "mainnet");
  PHX_EXPECT(cfg.value().get_or("rpc_port", "") == "8740");
  PHX_EXPECT(cfg.value().get_or("log_level", "") == "debug");
  std::filesystem::remove(path);
}

PHX_TEST(config_rejects_invalid_line) {
  const auto path = std::filesystem::temp_directory_path() / "phantomx-test-bad.cfg";
  {
    std::ofstream out(path);
    out << "bad_line_without_equals\n";
  }
  auto cfg = phantomx::Config::load_file(path);
  PHX_EXPECT(!cfg.ok());
  PHX_EXPECT(cfg.error().code() == phantomx::ErrorCode::kConfigError);
  std::filesystem::remove(path);
}
