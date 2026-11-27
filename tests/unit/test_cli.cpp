#include "../test_framework.hpp"
#include "phantomx/common/cli.hpp"

PHX_TEST(cli_parses_command_options_flags) {
  phantomx::CliParser parser("phantomx-cli");
  char arg0[] = "phantomx-cli";
  char arg1[] = "status";
  char arg2[] = "--log-level";
  char arg3[] = "debug";
  char arg4[] = "--help";
  char* argv[] = {arg0, arg1, arg2, arg3, arg4};
  auto parsed = parser.parse(5, argv);
  PHX_EXPECT(parsed.ok());
  PHX_EXPECT(parsed.value().command == "status");
  PHX_EXPECT(parsed.value().options.at("--log-level") == "debug");
  bool has_help = false;
  for (const auto& f : parsed.value().flags) if (f == "--help") has_help = true;
  PHX_EXPECT(has_help);
}

PHX_TEST(cli_rejects_unknown_option) {
  phantomx::CliParser parser("phantomx-cli");
  char arg0[] = "phantomx-cli";
  char arg1[] = "--nope";
  char* argv[] = {arg0, arg1};
  auto parsed = parser.parse(2, argv);
  PHX_EXPECT(!parsed.ok());
  PHX_EXPECT(parsed.error().code() == phantomx::ErrorCode::kInvalidArgument);
}
