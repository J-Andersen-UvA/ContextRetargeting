#include "test_support.hpp"

#include <exception>
#include <iostream>

int main()
{
    std::size_t failures = 0;

    for (const auto& test : test_support::registry()) {
        try {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }

    std::cout << test_support::registry().size() - failures
              << "/" << test_support::registry().size()
              << " tests passed\n";

    return failures == 0 ? 0 : 1;
}
