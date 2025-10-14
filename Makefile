# Hybrid Chain-Quorum Replication Protocol Makefile
# Simple build system for macOS/Linux without CMake dependency

CXX = clang++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -I./include
LDFLAGS = -pthread

# Directories
SRC_DIR = src
BUILD_DIR = build
INCLUDE_DIR = include
TEST_DIR = tests

# Source files
CORE_SOURCES = $(SRC_DIR)/core/message.cpp $(SRC_DIR)/core/node.cpp
PROTOCOL_SOURCES = $(SRC_DIR)/protocols/chain_replication.cpp $(SRC_DIR)/protocols/quorum_replication.cpp $(SRC_DIR)/protocols/hybrid_protocol.cpp
NETWORK_SOURCES = $(SRC_DIR)/network/network_manager.cpp
PERFORMANCE_SOURCES = $(SRC_DIR)/performance/metrics.cpp
UTILS_SOURCES = $(SRC_DIR)/utils/logger.cpp

ALL_SOURCES = $(CORE_SOURCES) $(PROTOCOL_SOURCES) $(NETWORK_SOURCES) $(PERFORMANCE_SOURCES) $(UTILS_SOURCES)

# Object files
OBJECTS = $(ALL_SOURCES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

# Test sources
TEST_SOURCES = $(TEST_DIR)/test_main.cpp $(TEST_DIR)/test_chain_replication.cpp $(TEST_DIR)/test_quorum_replication.cpp $(TEST_DIR)/test_hybrid_protocol.cpp $(TEST_DIR)/test_performance.cpp

# Executables
MAIN_TARGET = $(BUILD_DIR)/replication_node
BENCHMARK_TARGET = $(BUILD_DIR)/benchmark
TEST_TARGET = $(BUILD_DIR)/run_tests
LIBRARY_TARGET = $(BUILD_DIR)/libhybrid_replication.a

# Default target
all: $(MAIN_TARGET) $(BENCHMARK_TARGET) $(TEST_TARGET) $(LIBRARY_TARGET)

# Create build directory structure
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/core
	@mkdir -p $(BUILD_DIR)/protocols
	@mkdir -p $(BUILD_DIR)/network
	@mkdir -p $(BUILD_DIR)/performance
	@mkdir -p $(BUILD_DIR)/utils

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# Build static library
$(LIBRARY_TARGET): $(OBJECTS) | $(BUILD_DIR)
	@echo "Creating static library $@..."
	@ar rcs $@ $^

# Build main executable
$(MAIN_TARGET): $(SRC_DIR)/main.cpp $(LIBRARY_TARGET) | $(BUILD_DIR)
	@echo "Building main executable $@..."
	@$(CXX) $(CXXFLAGS) -o $@ $< -L$(BUILD_DIR) -lhybrid_replication $(LDFLAGS)

# Build benchmark executable
$(BENCHMARK_TARGET): $(SRC_DIR)/benchmark.cpp $(LIBRARY_TARGET) | $(BUILD_DIR)
	@echo "Building benchmark executable $@..."
	@$(CXX) $(CXXFLAGS) -o $@ $< -L$(BUILD_DIR) -lhybrid_replication $(LDFLAGS)

# Build test executable
$(TEST_TARGET): $(TEST_SOURCES) $(LIBRARY_TARGET) | $(BUILD_DIR)
	@echo "Building test executable $@..."
	@$(CXX) $(CXXFLAGS) -o $@ $^ -L$(BUILD_DIR) -lhybrid_replication $(LDFLAGS)

# Run tests
test: $(TEST_TARGET)
	@echo "Running test suite..."
	@$(TEST_TARGET)

# Run benchmark
benchmark: $(BENCHMARK_TARGET)
	@echo "Running benchmark..."
	@$(BENCHMARK_TARGET) --nodes 5 --threads 4 --ops 1000

# Run demo
demo: $(MAIN_TARGET)
	@echo "Running demo..."
	@$(MAIN_TARGET) --node-id 1 --demo

# Clean build artifacts
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)

# Install (copy to /usr/local/bin)
install: $(MAIN_TARGET) $(BENCHMARK_TARGET)
	@echo "Installing executables..."
	@sudo cp $(MAIN_TARGET) /usr/local/bin/
	@sudo cp $(BENCHMARK_TARGET) /usr/local/bin/

# Show build info
info:
	@echo "Hybrid Chain-Quorum Replication Protocol Build System"
	@echo "====================================================="
	@echo "Compiler: $(CXX)"
	@echo "Flags: $(CXXFLAGS)"
	@echo "Sources: $(words $(ALL_SOURCES)) files"
	@echo "Tests: $(words $(TEST_SOURCES)) files"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build all executables and library"
	@echo "  test      - Build and run test suite"
	@echo "  benchmark - Build and run performance benchmark"
	@echo "  demo      - Build and run demo workload"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install to /usr/local/bin"
	@echo "  info      - Show this information"

# Show help
help: info

.PHONY: all test benchmark demo clean install info help

# Dependencies (simplified - in production would use automatic dependency generation)
$(BUILD_DIR)/core/message.o: $(INCLUDE_DIR)/core/message.h
$(BUILD_DIR)/core/node.o: $(INCLUDE_DIR)/core/node.h $(INCLUDE_DIR)/core/message.h
$(BUILD_DIR)/protocols/chain_replication.o: $(INCLUDE_DIR)/protocols/chain_replication.h $(INCLUDE_DIR)/core/node.h
$(BUILD_DIR)/protocols/quorum_replication.o: $(INCLUDE_DIR)/protocols/quorum_replication.h $(INCLUDE_DIR)/core/node.h
$(BUILD_DIR)/protocols/hybrid_protocol.o: $(INCLUDE_DIR)/protocols/hybrid_protocol.h $(INCLUDE_DIR)/protocols/chain_replication.h $(INCLUDE_DIR)/protocols/quorum_replication.h
$(BUILD_DIR)/network/network_manager.o: $(INCLUDE_DIR)/network/network_manager.h $(INCLUDE_DIR)/core/message.h
$(BUILD_DIR)/performance/metrics.o: $(INCLUDE_DIR)/performance/metrics.h $(INCLUDE_DIR)/core/message.h
$(BUILD_DIR)/utils/logger.o: $(INCLUDE_DIR)/utils/logger.h 