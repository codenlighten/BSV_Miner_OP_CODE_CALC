cmake_minimum_required(VERSION 3.16)
project(bsv_script_bench CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags for accurate benchmarking
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# Find required packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# BSV node dependencies (adjust paths as needed)
# Option 1: Link against installed BSV node libraries
# find_library(BSV_CONSENSUS bitcoinconsensus PATHS deps/bitcoin-sv/lib)
# Option 2: Build against BSV source tree
# set(BSV_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/bitcoin-sv/src")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # ${BSV_SRC_DIR}
)

# Core benchmark harness library
add_library(bench_harness STATIC
    src/bench_harness.cpp
)
target_link_libraries(bench_harness
    Threads::Threads
)

# Individual benchmark executables
add_executable(bench_stack_ops src/bench_stack_ops.cpp)
target_link_libraries(bench_stack_ops bench_harness)

add_executable(bench_byte_ops src/bench_byte_ops.cpp)
target_link_libraries(bench_byte_ops bench_harness)

add_executable(bench_hash_ops src/bench_hash_ops.cpp)
target_link_libraries(bench_hash_ops bench_harness OpenSSL::Crypto)

add_executable(bench_sig_ops src/bench_sig_ops.cpp)
target_link_libraries(bench_sig_ops bench_harness)

add_executable(bench_control_flow src/bench_control_flow.cpp)
target_link_libraries(bench_control_flow bench_harness)

add_executable(bench_arithmetic src/bench_arithmetic.cpp)
target_link_libraries(bench_arithmetic bench_harness)

# All benchmarks target
add_custom_target(run_all_benchmarks
    COMMAND bench_stack_ops
    COMMAND bench_byte_ops
    COMMAND bench_hash_ops
    COMMAND bench_sig_ops
    COMMAND bench_control_flow
    COMMAND bench_arithmetic
    DEPENDS bench_stack_ops bench_byte_ops bench_hash_ops 
            bench_sig_ops bench_control_flow bench_arithmetic
)

# Install targets
install(TARGETS bench_stack_ops bench_byte_ops bench_hash_ops 
                bench_sig_ops bench_control_flow bench_arithmetic
        RUNTIME DESTINATION bin)
