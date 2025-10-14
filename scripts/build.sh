#!/bin/bash

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
BUILD_TYPE="Release"
BUILD_DIR="build"
NUM_JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
RUN_TESTS=false
RUN_BENCHMARK=false
CLEAN_BUILD=false

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -t, --type TYPE         Build type: Debug, Release, RelWithDebInfo (default: Release)"
    echo "  -d, --dir DIR           Build directory (default: build)"
    echo "  -j, --jobs N            Number of parallel jobs (default: auto-detect)"
    echo "  -c, --clean             Clean build directory before building"
    echo "  --test                  Run tests after building"
    echo "  --benchmark             Run benchmark after building"
    echo "  -h, --help              Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                      # Basic release build"
    echo "  $0 -t Debug --test      # Debug build with tests"
    echo "  $0 --clean --benchmark  # Clean build with benchmark"
    echo "  $0 -j 8 -t Release      # Release build with 8 parallel jobs"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -d|--dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -j|--jobs)
            NUM_JOBS="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        --test)
            RUN_TESTS=true
            shift
            ;;
        --benchmark)
            RUN_BENCHMARK=true
            shift
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Validate build type
case $BUILD_TYPE in
    Debug|Release|RelWithDebInfo|MinSizeRel)
        ;;
    *)
        print_error "Invalid build type: $BUILD_TYPE"
        print_error "Valid types: Debug, Release, RelWithDebInfo, MinSizeRel"
        exit 1
        ;;
esac

# Check if we're in the project root
if [ ! -f "CMakeLists.txt" ]; then
    print_error "CMakeLists.txt not found. Please run this script from the project root directory."
    exit 1
fi

print_status "Hybrid Chain-Quorum Replication Protocol Build Script"
print_status "=================================================="
print_status "Build type: $BUILD_TYPE"
print_status "Build directory: $BUILD_DIR"
print_status "Parallel jobs: $NUM_JOBS"
print_status "Clean build: $CLEAN_BUILD"
print_status "Run tests: $RUN_TESTS"
print_status "Run benchmark: $RUN_BENCHMARK"
echo ""

# Clean build directory if requested
if [ "$CLEAN_BUILD" = true ]; then
    print_status "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
if [ ! -d "$BUILD_DIR" ]; then
    print_status "Creating build directory: $BUILD_DIR"
    mkdir -p "$BUILD_DIR"
fi

# Change to build directory
cd "$BUILD_DIR"

# Configure with CMake
print_status "Configuring with CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DENABLE_WARNINGS=ON

if [ $? -ne 0 ]; then
    print_error "CMake configuration failed"
    exit 1
fi

print_success "CMake configuration completed"

# Build the project
print_status "Building project with $NUM_JOBS parallel jobs..."
make -j"$NUM_JOBS"

if [ $? -ne 0 ]; then
    print_error "Build failed"
    exit 1
fi

print_success "Build completed successfully"

# Show build artifacts
print_status "Build artifacts:"
echo "  - replication_node: Main application"
echo "  - benchmark: Performance benchmark tool"
echo "  - run_tests: Test suite runner"
echo "  - libhybrid_replication.a: Static library"

# Run tests if requested
if [ "$RUN_TESTS" = true ]; then
    print_status "Running test suite..."
    if [ -f "./run_tests" ]; then
        ./run_tests
        if [ $? -eq 0 ]; then
            print_success "All tests passed"
        else
            print_error "Some tests failed"
            exit 1
        fi
    else
        print_warning "Test executable not found"
    fi
fi

# Run benchmark if requested
if [ "$RUN_BENCHMARK" = true ]; then
    print_status "Running performance benchmark..."
    if [ -f "./benchmark" ]; then
        ./benchmark --nodes 5 --threads 4 --ops 1000
        if [ $? -eq 0 ]; then
            print_success "Benchmark completed"
        else
            print_warning "Benchmark completed with warnings"
        fi
    else
        print_warning "Benchmark executable not found"
    fi
fi

print_success "Build script completed successfully!"
echo ""
print_status "To run the application:"
echo "  cd $BUILD_DIR"
echo "  ./replication_node --node-id 1 --demo"
echo ""
print_status "To run tests:"
echo "  cd $BUILD_DIR"
echo "  ./run_tests"
echo ""
print_status "To run benchmark:"
echo "  cd $BUILD_DIR"
echo "  ./benchmark" 