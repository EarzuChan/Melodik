#pragma once

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace melodick::tests {

using TestFn = std::function<void()>;

struct TestCase {
    std::string name;
    TestFn fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests {};
    return tests;
}

struct TestRegistrar {
    TestRegistrar(std::string name, TestFn fn) {
        registry().push_back(TestCase {.name = std::move(name), .fn = std::move(fn)});
    }
};

inline int run_all() {
    int failed = 0;
    for (const auto& test : registry()) {
        try {
            test.fn();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& ex) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << " : " << ex.what() << '\n';
        }
    }
    std::cout << "total=" << registry().size() << " failed=" << failed << '\n';
    return failed == 0 ? 0 : 1;
}

} // namespace melodick::tests

#define MELODICK_TEST(name)                                                                                              \
    static void name();                                                                                                  \
    static ::melodick::tests::TestRegistrar name##_registrar {#name, name};                                             \
    static void name()

#define MELODICK_EXPECT_TRUE(expr)                                                                                       \
    do {                                                                                                                 \
        if (!(expr)) {                                                                                                   \
            throw std::runtime_error(std::string("expect true failed: ") + #expr);                                      \
        }                                                                                                                \
    } while (false)

#define MELODICK_EXPECT_EQ(lhs, rhs)                                                                                     \
    do {                                                                                                                 \
        if (!((lhs) == (rhs))) {                                                                                         \
            throw std::runtime_error(std::string("expect eq failed: ") + #lhs + " vs " + #rhs);                        \
        }                                                                                                                \
    } while (false)

