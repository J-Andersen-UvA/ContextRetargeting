add_executable(context_retargeting_optimization_tests
    test_main.cpp
    numerical_gradient_tests.cpp
    adam_tests.cpp
    minimize_tests.cpp
)

target_link_libraries(context_retargeting_optimization_tests
    PRIVATE ContextRetargeting::Optimization)

target_compile_features(context_retargeting_optimization_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(context_retargeting_optimization_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(context_retargeting_optimization_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME context_retargeting_optimization_tests
    COMMAND context_retargeting_optimization_tests)
