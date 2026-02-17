#pragma once

#if defined(THERMOSTAT_RUN_TESTS)

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace thermostat_tests {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase> &registry() {
  static std::vector<TestCase> tests;
  return tests;
}

inline void register_test(const std::string &name, std::function<void()> fn) {
  registry().push_back({name, fn});
}

inline int run_all_tests() {
  int failures = 0;
  for (const auto &t : registry()) {
    try {
      t.fn();
      std::cout << "[PASS] " << t.name << "\n";
    } catch (const std::exception &e) {
      ++failures;
      std::cout << "[FAIL] " << t.name << " :: " << e.what() << "\n";
    }
  }
  std::cout << "Tests run: " << registry().size() << ", failures: " << failures << "\n";
  return failures;
}

class AssertionError : public std::runtime_error {
 public:
  explicit AssertionError(const std::string &msg) : std::runtime_error(msg) {}
};

}  // namespace thermostat_tests

#define TEST_CASE(NAME)                                                         \
  static void NAME();                                                           \
  namespace {                                                                   \
  struct NAME##_registrar {                                                     \
    NAME##_registrar() {                                                        \
      thermostat_tests::register_test(#NAME, NAME);                             \
    }                                                                           \
  } NAME##_registrar_instance;                                                  \
  }                                                                             \
  static void NAME()

#define ASSERT_TRUE(EXPR)                                                       \
  do {                                                                          \
    if (!(EXPR)) {                                                              \
      throw thermostat_tests::AssertionError(std::string("assert true failed: ") + #EXPR); \
    }                                                                           \
  } while (0)

#define ASSERT_EQ(A, B)                                                         \
  do {                                                                          \
    const auto _a = (A);                                                        \
    const auto _b = (B);                                                        \
    if (!(_a == _b)) {                                                          \
      throw thermostat_tests::AssertionError(std::string("assert eq failed: ") + #A + " != " + #B); \
    }                                                                           \
  } while (0)

#define ASSERT_NEAR(A, B, EPS)                                                  \
  do {                                                                          \
    const auto _a = static_cast<double>(A);                                     \
    const auto _b = static_cast<double>(B);                                     \
    const auto _eps = static_cast<double>(EPS);                                 \
    if (std::fabs(_a - _b) > _eps) {                                            \
      throw thermostat_tests::AssertionError(std::string("assert near failed: ") + #A + " vs " + #B); \
    }                                                                           \
  } while (0)

#endif
