cmake_minimum_required(VERSION 3.16)
project(libbsv_cost_estimator LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# Find nlohmann/json (header-only library)
# Option 1: System-installed
find_package(nlohmann_json 3.2.0 QUIET)

# Option 2: Fetch from GitHub if not found
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    )
    FetchContent_MakeAvailable(json)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Main library
add_library(bsv_cost_estimator STATIC
    src/cost_estimator.cpp
)

target_link_libraries(bsv_cost_estimator
    PRIVATE nlohmann_json::nlohmann_json
)

target_include_directories(bsv_cost_estimator
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Example executable
add_executable(estimate_tx examples/estimate_tx.cpp)
target_link_libraries(estimate_tx bsv_cost_estimator nlohmann_json::nlohmann_json)

# Tests
enable_testing()
add_executable(test_estimator tests/test_estimator.cpp)
target_link_libraries(test_estimator bsv_cost_estimator nlohmann_json::nlohmann_json)
add_test(NAME test_estimator COMMAND test_estimator)

# Install targets
install(TARGETS bsv_cost_estimator
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
        
install(DIRECTORY include/bsv
        DESTINATION include)

install(TARGETS estimate_tx
        RUNTIME DESTINATION bin)
