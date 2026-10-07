cmake_minimum_required(VERSION 3.20)

project(ContextRetargetingContextSolver LANGUAGES CXX)

option(CONTEXT_RETARGETING_CONTEXT_BUILD_TESTS
       "Build the Context Solver unit tests"
       ON)

if(NOT TARGET ContextRetargeting::Optimization)
    set(CONTEXT_RETARGETING_OPTIMIZATION_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../Optimization"
        "${CMAKE_CURRENT_BINARY_DIR}/Optimization"
        EXCLUDE_FROM_ALL)
endif()

add_library(context_retargeting_context_solver
    src/context_objective.cpp
    src/context_solver.cpp
    src/geometry.cpp
    src/pose.cpp
)

add_library(ContextRetargeting::ContextSolver
    ALIAS context_retargeting_context_solver)

target_compile_features(context_retargeting_context_solver PUBLIC cxx_std_20)

target_include_directories(context_retargeting_context_solver
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(context_retargeting_context_solver
    PUBLIC ContextRetargeting::Optimization)

if(MSVC)
    target_compile_options(context_retargeting_context_solver PRIVATE /W4 /permissive-)
else()
    target_compile_options(context_retargeting_context_solver PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CONTEXT_RETARGETING_CONTEXT_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
