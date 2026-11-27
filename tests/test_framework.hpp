#pragma once

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
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
  // Optional per-test watchdog. CI sets PHX_TEST_TIMEOUT_MS so a hung test is
  // reported by name instead of blocking the job forever.
  unsigned long long timeout_ms = 0;
  if (const char* env = std::getenv("PHX_TEST_TIMEOUT_MS")) {
    try { timeout_ms = std::stoull(env); } catch (...) { timeout_ms = 0; }
  }

  std::atomic<std::size_t> current_index{0};
  std::atomic<long long> test_started_ms{0};
  std::atomic<bool> finished{false};
  std::thread watchdog;

  if (timeout_ms > 0) {
    watchdog = std::thread([&]() {
      while (!finished.load(std::memory_order_relaxed)) {
        const auto start = test_started_ms.load(std::memory_order_relaxed);
        if (start != 0) {
          const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count();
          if (static_cast<unsigned long long>(now - start) >= timeout_ms) {
            const auto idx = current_index.load(std::memory_order_relaxed);
            std::cerr << "TIMEOUT: " << registry()[idx].name
                      << " (" << timeout_ms << " ms)" << std::endl;
            std::cerr.flush();
            // Avoid an orderly shutdown that could itself block in a deadlocked
            // test; report the name and terminate immediately.
            std::_Exit(3);
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });
  }

  int passed = 0;
  for (std::size_t i = 0; i < registry().size(); ++i) {
    const auto& t = registry()[i];
    current_index.store(i, std::memory_order_relaxed);
    test_started_ms.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(),
        std::memory_order_relaxed);

    // Flush each result so a crash still leaves the last executed test visible
    // in captured CI output. Elapsed time also exposes slow tests.
    const auto t0 = std::chrono::steady_clock::now();
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
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0).count();
    if (failures_ref() == before) {
      std::cout << "PASS: " << t.name << " (" << elapsed_ms << " ms)" << std::endl;
      ++passed;
    } else {
      std::cout << "FAIL: " << t.name << " (" << elapsed_ms << " ms)" << std::endl;
    }
  }
  finished.store(true, std::memory_order_relaxed);
  if (watchdog.joinable()) watchdog.join();

  std::cout << "\n" << passed << " passed, " << failures_ref() << " failed, "
            << registry().size() << " total" << std::endl;
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
