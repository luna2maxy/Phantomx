#include "test_framework.hpp"
#include "phantomx/common/types.hpp"

PHX_TEST(atoms_to_phx_string_formats) {
  PHX_EXPECT(phantomx::atoms_to_phx_string(0) == "0.00000000");
  PHX_EXPECT(phantomx::atoms_to_phx_string(1) == "0.00000001");
  PHX_EXPECT(phantomx::atoms_to_phx_string(phantomx::kPhxAtomsPerPhx) == "1.00000000");
  PHX_EXPECT(phantomx::atoms_to_phx_string(2'500'000'000ULL) == "25.00000000");
}

PHX_TEST(parse_phx_atoms_roundtrip) {
  phantomx::PhxAtom atoms = 0;
  PHX_EXPECT(phantomx::parse_phx_atoms("25.00000000", atoms));
  PHX_EXPECT_EQ(atoms, 2'500'000'000ULL);
  PHX_EXPECT(phantomx::parse_phx_atoms("0.1", atoms));
  PHX_EXPECT_EQ(atoms, 10'000'000ULL);
  PHX_EXPECT(phantomx::parse_phx_atoms("1", atoms));
  PHX_EXPECT_EQ(atoms, 100'000'000ULL);
}

PHX_TEST(parse_phx_atoms_rejects_invalid) {
  phantomx::PhxAtom atoms = 0;
  PHX_EXPECT(!phantomx::parse_phx_atoms("", atoms));
  PHX_EXPECT(!phantomx::parse_phx_atoms("abc", atoms));
  PHX_EXPECT(!phantomx::parse_phx_atoms("1.123456789", atoms));
  PHX_EXPECT(!phantomx::parse_phx_atoms("-1", atoms));
}

PHX_TEST(version_to_string) {
  phantomx::Version v{1, 2, 3};
  PHX_EXPECT(v.to_string() == "1.2.3");
}
