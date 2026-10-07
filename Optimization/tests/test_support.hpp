#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace test_support {

struct TestCase
{
    const char* name;
    std::function<void()> run;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

class Registrar
{
public:
    Registrar(const char* name, std::function<void()> run)
    {
        registry().push_back({name, std::move(run)});
    }
};

inline void require(bool condition, const char* expression, int line)
{
    if (!condition) {
        std::ostringstream message;
        message << "Requirement failed at line " << line << ": " << expression;
        throw std::runtime_error(message.str());
    }
}

inline void requireNear(
    double actual,
    double expected,
    double tolerance,
    int line)
{
    const double scale = std::max({1.0, std::abs(actual), std::abs(expected)});
    if (!std::isfinite(actual) ||
        std::abs(actual - expected) > tolerance * scale) {
        std::ostringstream message;
        message << "Near comparison failed at line " << line
                << ": actual=" << actual
                << ", expected=" << expected
                << ", tolerance=" << tolerance;
        throw std::runtime_error(message.str());
    }
}

template <typename Exception, typename Function>
void requireThrows(Function&& function, int line)
{
    try {
        function();
    } catch (const Exception&) {
        return;
    } catch (...) {
        throw std::runtime_error(
            "Unexpected exception type at line " + std::to_string(line));
    }
    throw std::runtime_error(
        "Expected exception was not thrown at line " + std::to_string(line));
}

} // namespace test_support

#define CRT_DETAIL_JOIN_IMPL(left, right) left##right
#define CRT_DETAIL_JOIN(left, right) CRT_DETAIL_JOIN_IMPL(left, right)

#define CRT_TEST(name)                                                        \
    static void CRT_DETAIL_JOIN(testFunction_, __LINE__)();                   \
    static test_support::Registrar CRT_DETAIL_JOIN(testRegistrar_, __LINE__)( \
        name, CRT_DETAIL_JOIN(testFunction_, __LINE__));                      \
    static void CRT_DETAIL_JOIN(testFunction_, __LINE__)()

#define CRT_REQUIRE(expression) \
    test_support::require((expression), #expression, __LINE__)

#define CRT_REQUIRE_NEAR(actual, expected, tolerance) \
    test_support::requireNear((actual), (expected), (tolerance), __LINE__)

#define CRT_REQUIRE_THROWS(exceptionType, expression)                         \
    test_support::requireThrows<exceptionType>([&] { (void)(expression); }, __LINE__)
