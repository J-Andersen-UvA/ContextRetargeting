add_executable(context_retargeting_context_solver_tests
    test_main.cpp
    geometry_tests.cpp
    context_objective_tests.cpp
    context_solver_tests.cpp
)

target_link_libraries(context_retargeting_context_solver_tests
    PRIVATE ContextRetargeting::ContextSolver)

target_compile_features(context_retargeting_context_solver_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(context_retargeting_context_solver_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(context_retargeting_context_solver_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME context_retargeting_context_solver_tests
    COMMAND context_retargeting_context_solver_tests)
