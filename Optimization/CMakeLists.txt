cmake_minimum_required(VERSION 3.20)

project(ContextRetargetingOptimization LANGUAGES CXX)

option(CONTEXT_RETARGETING_OPTIMIZATION_BUILD_TESTS
       "Build the Optimization unit tests"
       ON)

add_library(context_retargeting_optimization
    src/adam.cpp
    src/minimize.cpp
    src/numerical_gradient.cpp
)

add_library(ContextRetargeting::Optimization
    ALIAS context_retargeting_optimization)

target_compile_features(context_retargeting_optimization PUBLIC cxx_std_20)

target_include_directories(context_retargeting_optimization
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(context_retargeting_optimization PRIVATE /W4 /permissive-)
else()
    target_compile_options(context_retargeting_optimization PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CONTEXT_RETARGETING_OPTIMIZATION_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()
