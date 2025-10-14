cmake_minimum_required(VERSION 3.16)
project(HybridChainQuorumReplication VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)

# Compiler flags for optimization
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/core/message.cpp
    src/core/node.cpp
    src/protocols/chain_replication.cpp
    src/protocols/quorum_replication.cpp
    src/protocols/hybrid_protocol.cpp
    src/network/network_manager.cpp
    src/performance/metrics.cpp
    src/utils/logger.cpp
)

# Create the main library
add_library(hybrid_replication ${SOURCES})
target_link_libraries(hybrid_replication Threads::Threads)

# Main executable
add_executable(replication_node src/main.cpp)
target_link_libraries(replication_node hybrid_replication)

# Test executable
add_executable(run_tests 
    tests/test_chain_replication.cpp
    tests/test_quorum_replication.cpp
    tests/test_hybrid_protocol.cpp
    tests/test_performance.cpp
)
target_link_libraries(run_tests hybrid_replication)

# Benchmark executable
add_executable(benchmark src/benchmark.cpp)
target_link_libraries(benchmark hybrid_replication)

# Install targets
install(TARGETS replication_node DESTINATION bin)
install(TARGETS hybrid_replication DESTINATION lib)
install(DIRECTORY include/ DESTINATION include) 