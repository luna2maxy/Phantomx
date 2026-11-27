#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace phantomx::test {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct Registrar {
  Registrar(std::string name, std::function<void()> fn) {
    registry().push_back({std::move(name), std::move(fn)});
  }
};

inline int failures() {
  static int count = 0;
  return count;
}

inline int& failures_ref() {
  static int count = 0;
  return count;
}

inline void fail(const std::string& expr, const char* file, int line) {
  std::cerr << "FAIL: " << file << ":" << line << "  " << expr << std::endl;
  ++failures_ref();
}

inline int run_all() {
  int passed = 0;
  for (const auto& t : registry()) {
    // Flush each result so a crash still leaves the last executed test visible
    // in captured CI output.
    std::cout << "RUN: " << t.name << std::endl;
    const int before = failures_ref();
    try {
      t.fn();
    } catch (const std::exception& e) {
      std::cerr << "FAIL: " << t.name << " threw exception: " << e.what() << std::endl;
      ++failures_ref();
    } catch (...) {
      std::cerr << "FAIL: " << t.name << " threw unknown exception" << std::endl;
      ++failures_ref();
    }
    if (failures_ref() == before) {
      std::cout << "PASS: " << t.name << std::endl;
      ++passed;
    } else {
      std::cout << "FAIL: " << t.name << std::endl;
    }
  }
  std::cout << "\n" << passed << " passed, " << failures_ref() << " failed, " << registry().size() << " total" << std::endl;
  return failures_ref() == 0 ? 0 : 1;
}

}  // namespace phantomx::test

#define PHX_TEST(name) \
  static void phx_test_##name(); \
  static ::phantomx::test::Registrar phx_reg_##name(#name, &phx_test_##name); \
  static void phx_test_##name()

#define PHX_EXPECT(cond) \
  do { \
    if (!(cond)) ::phantomx::test::fail(#cond, __FILE__, __LINE__); \
  } while (0)

#define PHX_EXPECT_EQ(a, b) \
  do { \
    if (!((a) == (b))) ::phantomx::test::fail(std::string(#a " == " #b) + " (" + std::to_string(static_cast<long long>(a)) + " vs " + std::to_string(static_cast<long long>(b)) + ")", __FILE__, __LINE__); \
  } while (0)
