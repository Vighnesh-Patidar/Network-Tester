#pragma once

// Minimal GoogleTest-compatible test harness used for offline builds where the
// upstream GTest package is not installed. The build prefers system GTest via
// find_package(GTest); this vendored shim is the fallback. It implements the
// subset of the GoogleTest macro surface the test suite uses (TEST, TEST_F,
// EXPECT_*/ASSERT_* comparisons, EXPECT_THROW, fixtures, RUN_ALL_TESTS) so the
// test sources do not have to change between the two backends.

#include <cmath>
#include <functional>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace testing {

struct TestFailure {};  // thrown by ASSERT_* to abort the current test only

namespace detail {

struct TestState {
    bool failed = false;
    int assertions = 0;
};

inline TestState& current_state() {
    static thread_local TestState state;
    return state;
}

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> body) {
        registry().push_back({suite, name, std::move(body)});
    }
};

inline void report_failure(const char* file, int line, const std::string& message) {
    current_state().failed = true;
    std::cerr << file << ":" << line << ": Failure\n" << message << "\n";
}

template <typename T, typename = void>
struct is_streamable : std::false_type {};

template <typename T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream&>()
                                             << std::declval<const T&>())>>
    : std::true_type {};

// Not every comparable type is printable (containers, enums, structs). Render
// what we can and degrade gracefully otherwise so a failed comparison still
// produces a usable message instead of a compile error.
template <typename T>
void stream_value(std::ostream& os, const T& value) {
    if constexpr (is_streamable<T>::value) {
        os << value;
    } else if constexpr (std::is_enum<T>::value) {
        os << static_cast<long long>(value);
    } else {
        os << "<unprintable>";
    }
}

template <typename A, typename B>
std::string format_binary(const char* expr_a, const char* expr_b, const A& a, const B& b) {
    std::ostringstream os;
    os << "  Expected: " << expr_a << "\n  Which is: ";
    stream_value(os, a);
    os << "\n  vs:       " << expr_b << "\n  Which is: ";
    stream_value(os, b);
    return os.str();
}

}  // namespace detail

class Test {
public:
    virtual ~Test() = default;
    virtual void TestBody() = 0;

protected:
    virtual void SetUp() {}
    virtual void TearDown() {}

private:
    template <typename T>
    friend void run_fixture();
    void Run() {
        SetUp();
        TestBody();
        TearDown();
    }

    template <typename T>
    friend struct FixtureRunner;
};

template <typename T>
struct FixtureRunner {
    static void run() {
        T fixture;
        fixture.SetUp();
        try {
            fixture.TestBody();
        } catch (...) {
            fixture.TearDown();
            throw;
        }
        fixture.TearDown();
    }
};

inline void InitGoogleTest(int* /*argc*/, char** /*argv*/) {}

inline int RunAllTests() {
    int passed = 0;
    int failed = 0;
    auto& cases = detail::registry();
    std::cout << "[==========] Running " << cases.size() << " tests.\n";
    for (const auto& tc : cases) {
        detail::current_state() = detail::TestState{};
        const std::string full = tc.suite + "." + tc.name;
        std::cout << "[ RUN      ] " << full << "\n";
        try {
            tc.body();
        } catch (const TestFailure&) {
            // Already recorded by the ASSERT_* macro that threw.
        } catch (const std::exception& ex) {
            detail::report_failure(__FILE__, __LINE__,
                                   std::string("Uncaught exception: ") + ex.what());
        } catch (...) {
            detail::report_failure(__FILE__, __LINE__, "Uncaught non-standard exception");
        }
        if (detail::current_state().failed) {
            ++failed;
            std::cout << "[  FAILED  ] " << full << "\n";
        } else {
            ++passed;
            std::cout << "[       OK ] " << full << "\n";
        }
    }
    std::cout << "[==========] " << cases.size() << " tests ran.\n";
    std::cout << "[  PASSED  ] " << passed << " tests.\n";
    if (failed > 0) {
        std::cout << "[  FAILED  ] " << failed << " tests.\n";
    }
    return failed == 0 ? 0 : 1;
}

}  // namespace testing

#define GTEST_CONCAT_(a, b) a##b
#define GTEST_CONCAT(a, b) GTEST_CONCAT_(a, b)

#define TEST(suite_name, test_name)                                                       \
    static void suite_name##_##test_name##_body();                                        \
    static ::testing::detail::Registrar GTEST_CONCAT(reg_, __LINE__)(                      \
        #suite_name, #test_name, &suite_name##_##test_name##_body);                        \
    static void suite_name##_##test_name##_body()

#define TEST_F(fixture_name, test_name)                                                   \
    struct fixture_name##_##test_name : public fixture_name {                             \
        void TestBody() override;                                                         \
    };                                                                                    \
    static ::testing::detail::Registrar GTEST_CONCAT(reg_, __LINE__)(                      \
        #fixture_name, #test_name,                                                        \
        [] { ::testing::FixtureRunner<fixture_name##_##test_name>::run(); });             \
    void fixture_name##_##test_name::TestBody()

#define GTEST_MESSAGE_AT(file, line, msg) ::testing::detail::report_failure(file, line, msg)

#define EXPECT_TRUE(cond)                                                                 \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            GTEST_MESSAGE_AT(__FILE__, __LINE__, std::string("Expected true: ") + #cond); \
        }                                                                                 \
    } while (0)

#define EXPECT_FALSE(cond)                                                                \
    do {                                                                                  \
        if ((cond)) {                                                                     \
            GTEST_MESSAGE_AT(__FILE__, __LINE__, std::string("Expected false: ") + #cond);\
        }                                                                                 \
    } while (0)

#define GTEST_BINARY_(a, b, op, file, line)                                               \
    do {                                                                                  \
        auto&& gtest_a = (a);                                                             \
        auto&& gtest_b = (b);                                                             \
        if (!(gtest_a op gtest_b)) {                                                      \
            GTEST_MESSAGE_AT(file, line,                                                  \
                             ::testing::detail::format_binary(#a, #b, gtest_a, gtest_b)); \
        }                                                                                 \
    } while (0)

#define EXPECT_EQ(a, b) GTEST_BINARY_(a, b, ==, __FILE__, __LINE__)
#define EXPECT_NE(a, b) GTEST_BINARY_(a, b, !=, __FILE__, __LINE__)
#define EXPECT_LT(a, b) GTEST_BINARY_(a, b, <, __FILE__, __LINE__)
#define EXPECT_LE(a, b) GTEST_BINARY_(a, b, <=, __FILE__, __LINE__)
#define EXPECT_GT(a, b) GTEST_BINARY_(a, b, >, __FILE__, __LINE__)
#define EXPECT_GE(a, b) GTEST_BINARY_(a, b, >=, __FILE__, __LINE__)

#define EXPECT_NEAR(a, b, tol)                                                            \
    do {                                                                                  \
        double gtest_diff = std::fabs(static_cast<double>(a) - static_cast<double>(b));   \
        if (gtest_diff > static_cast<double>(tol)) {                                      \
            std::ostringstream gtest_os;                                                  \
            gtest_os << "  |" << #a << " - " << #b << "| = " << gtest_diff                \
                     << " exceeds " << (tol);                                             \
            GTEST_MESSAGE_AT(__FILE__, __LINE__, gtest_os.str());                         \
        }                                                                                 \
    } while (0)

#define EXPECT_DOUBLE_EQ(a, b) EXPECT_NEAR(a, b, 1e-9)

#define EXPECT_THROW(stmt, exception_type)                                                \
    do {                                                                                  \
        bool gtest_threw = false;                                                         \
        try {                                                                             \
            stmt;                                                                         \
        } catch (const exception_type&) {                                                 \
            gtest_threw = true;                                                           \
        } catch (...) {                                                                   \
            GTEST_MESSAGE_AT(__FILE__, __LINE__,                                          \
                             std::string("Threw a different type: ") + #stmt);            \
            gtest_threw = true;                                                           \
        }                                                                                 \
        if (!gtest_threw) {                                                               \
            GTEST_MESSAGE_AT(__FILE__, __LINE__,                                          \
                             std::string("Expected throw: ") + #stmt);                    \
        }                                                                                 \
    } while (0)

#define EXPECT_NO_THROW(stmt)                                                             \
    do {                                                                                  \
        try {                                                                             \
            stmt;                                                                         \
        } catch (...) {                                                                   \
            GTEST_MESSAGE_AT(__FILE__, __LINE__,                                          \
                             std::string("Unexpected throw: ") + #stmt);                  \
        }                                                                                 \
    } while (0)

#define ASSERT_TRUE(cond)                                                                 \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            GTEST_MESSAGE_AT(__FILE__, __LINE__, std::string("Expected true: ") + #cond); \
            throw ::testing::TestFailure{};                                               \
        }                                                                                 \
    } while (0)

#define ASSERT_FALSE(cond)                                                                \
    do {                                                                                  \
        if ((cond)) {                                                                     \
            GTEST_MESSAGE_AT(__FILE__, __LINE__, std::string("Expected false: ") + #cond);\
            throw ::testing::TestFailure{};                                               \
        }                                                                                 \
    } while (0)

#define GTEST_BINARY_FATAL_(a, b, op)                                                     \
    do {                                                                                  \
        auto&& gtest_a = (a);                                                             \
        auto&& gtest_b = (b);                                                             \
        if (!(gtest_a op gtest_b)) {                                                      \
            GTEST_MESSAGE_AT(__FILE__, __LINE__,                                          \
                             ::testing::detail::format_binary(#a, #b, gtest_a, gtest_b)); \
            throw ::testing::TestFailure{};                                               \
        }                                                                                 \
    } while (0)

#define ASSERT_EQ(a, b) GTEST_BINARY_FATAL_(a, b, ==)
#define ASSERT_NE(a, b) GTEST_BINARY_FATAL_(a, b, !=)
#define ASSERT_LT(a, b) GTEST_BINARY_FATAL_(a, b, <)
#define ASSERT_LE(a, b) GTEST_BINARY_FATAL_(a, b, <=)
#define ASSERT_GT(a, b) GTEST_BINARY_FATAL_(a, b, >)
#define ASSERT_GE(a, b) GTEST_BINARY_FATAL_(a, b, >=)

#define RUN_ALL_TESTS() ::testing::RunAllTests()
