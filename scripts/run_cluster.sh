#!/bin/bash

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default configuration
NUM_NODES=5
BASE_PORT=8080
BUILD_DIR="build"
LOG_LEVEL="info"
MODE="hybrid"
DEMO_WORKLOAD=false
BENCHMARK=false

# Arrays to store process IDs
declare -a NODE_PIDS=()

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
    echo "  -n, --nodes N           Number of nodes in cluster (default: 5)"
    echo "  -p, --port PORT         Base port number (default: 8080)"
    echo "  -d, --dir DIR           Build directory (default: build)"
    echo "  -l, --log-level LEVEL   Log level: debug, info, warn, error (default: info)"
    echo "  -m, --mode MODE         Replication mode: chain, quorum, hybrid (default: hybrid)"
    echo "  --demo                  Run demo workload on node 1"
    echo "  --benchmark             Run benchmark on node 1"
    echo "  --stop                  Stop running cluster"
    echo "  -h, --help              Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                      # Start 5-node hybrid cluster"
    echo "  $0 -n 3 --demo          # Start 3-node cluster with demo"
    echo "  $0 --benchmark          # Start cluster and run benchmark"
    echo "  $0 --stop               # Stop running cluster"
}

# Function to cleanup processes
cleanup() {
    print_status "Shutting down cluster..."
    
    # Kill all node processes
    for pid in "${NODE_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            print_status "Stopping node with PID $pid"
            kill -TERM "$pid" 2>/dev/null || true
        fi
    done
    
    # Wait for graceful shutdown
    sleep 2
    
    # Force kill if necessary
    for pid in "${NODE_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            print_warning "Force killing node with PID $pid"
            kill -KILL "$pid" 2>/dev/null || true
        fi
    done
    
    # Remove PID file
    rm -f cluster.pids
    
    print_success "Cluster shutdown complete"
}

# Function to stop existing cluster
stop_cluster() {
    if [ -f "cluster.pids" ]; then
        print_status "Stopping existing cluster..."
        while IFS= read -r pid; do
            if kill -0 "$pid" 2>/dev/null; then
                print_status "Stopping node with PID $pid"
                kill -TERM "$pid" 2>/dev/null || true
            fi
        done < cluster.pids
        
        sleep 2
        
        # Force kill if necessary
        while IFS= read -r pid; do
            if kill -0 "$pid" 2>/dev/null; then
                print_warning "Force killing node with PID $pid"
                kill -KILL "$pid" 2>/dev/null || true
            fi
        done < cluster.pids
        
        rm -f cluster.pids
        print_success "Existing cluster stopped"
    else
        print_warning "No cluster PID file found"
    fi
    exit 0
}

# Set up signal handlers
trap cleanup EXIT INT TERM

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -n|--nodes)
            NUM_NODES="$2"
            shift 2
            ;;
        -p|--port)
            BASE_PORT="$2"
            shift 2
            ;;
        -d|--dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -l|--log-level)
            LOG_LEVEL="$2"
            shift 2
            ;;
        -m|--mode)
            MODE="$2"
            shift 2
            ;;
        --demo)
            DEMO_WORKLOAD=true
            shift
            ;;
        --benchmark)
            BENCHMARK=true
            shift
            ;;
        --stop)
            stop_cluster
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

# Validate parameters
if [ "$NUM_NODES" -lt 3 ]; then
    print_error "Minimum 3 nodes required for proper replication"
    exit 1
fi

if [ "$NUM_NODES" -gt 10 ]; then
    print_error "Maximum 10 nodes supported by this script"
    exit 1
fi

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    print_error "Build directory not found: $BUILD_DIR"
    print_error "Please run the build script first: ./scripts/build.sh"
    exit 1
fi

# Check if replication_node executable exists
if [ ! -f "$BUILD_DIR/replication_node" ]; then
    print_error "replication_node executable not found in $BUILD_DIR"
    print_error "Please build the project first: ./scripts/build.sh"
    exit 1
fi

# Stop any existing cluster
if [ -f "cluster.pids" ]; then
    print_warning "Existing cluster found. Stopping it first..."
    stop_cluster
fi

print_status "Hybrid Chain-Quorum Replication Cluster Launcher"
print_status "================================================="
print_status "Number of nodes: $NUM_NODES"
print_status "Base port: $BASE_PORT"
print_status "Replication mode: $MODE"
print_status "Log level: $LOG_LEVEL"
print_status "Demo workload: $DEMO_WORKLOAD"
print_status "Benchmark: $BENCHMARK"
echo ""

# Create peer lists for each node
declare -a PEER_LISTS=()
for i in $(seq 1 "$NUM_NODES"); do
    peers=""
    for j in $(seq 1 "$NUM_NODES"); do
        if [ "$i" -ne "$j" ]; then
            if [ -n "$peers" ]; then
                peers="$peers,$j"
            else
                peers="$j"
            fi
        fi
    done
    PEER_LISTS[$i]="$peers"
done

# Start each node
print_status "Starting cluster nodes..."
for i in $(seq 1 "$NUM_NODES"); do
    port=$((BASE_PORT + i - 1))
    log_file="node_${i}.log"
    
    # Build command
    cmd="$BUILD_DIR/replication_node"
    cmd="$cmd --node-id $i"
    cmd="$cmd --port $port"
    cmd="$cmd --peers ${PEER_LISTS[$i]}"
    cmd="$cmd --mode $MODE"
    cmd="$cmd --log-level $LOG_LEVEL"
    cmd="$cmd --log-file $log_file"
    
    # Add demo or benchmark for node 1
    if [ "$i" -eq 1 ]; then
        if [ "$DEMO_WORKLOAD" = true ]; then
            cmd="$cmd --demo"
        elif [ "$BENCHMARK" = true ]; then
            cmd="$cmd --benchmark"
        fi
    fi
    
    print_status "Starting node $i on port $port (log: $log_file)"
    
    # Start the node in background
    $cmd > "$log_file" 2>&1 &
    pid=$!
    NODE_PIDS[$i]=$pid
    
    # Save PID to file for cleanup
    echo "$pid" >> cluster.pids
    
    print_success "Node $i started with PID $pid"
    
    # Brief delay between starts
    sleep 1
done

print_success "All nodes started successfully!"
echo ""

# Show cluster status
print_status "Cluster Status:"
echo "┌──────┬─────────┬───────┬─────────────┐"
echo "│ Node │  Port   │  PID  │    Status   │"
echo "├──────┼─────────┼───────┼─────────────┤"
for i in $(seq 1 "$NUM_NODES"); do
    port=$((BASE_PORT + i - 1))
    pid=${NODE_PIDS[$i]}
    if kill -0 "$pid" 2>/dev/null; then
        status="Running"
    else
        status="Failed"
    fi
    printf "│  %2d  │  %5d  │ %5d │ %-11s │\n" "$i" "$port" "$pid" "$status"
done
echo "└──────┴─────────┴───────┴─────────────┘"
echo ""

# Show connection information
print_status "Connection Information:"
echo "  Node endpoints:"
for i in $(seq 1 "$NUM_NODES"); do
    port=$((BASE_PORT + i - 1))
    echo "    Node $i: localhost:$port"
done
echo ""

# Show useful commands
print_status "Useful Commands:"
echo "  Monitor logs:     tail -f node_1.log"
echo "  Check processes:  ps aux | grep replication_node"
echo "  Stop cluster:     $0 --stop"
echo "  Node status:      echo 'status' | nc localhost $BASE_PORT"
echo ""

# Wait for user input or workload completion
if [ "$DEMO_WORKLOAD" = true ] || [ "$BENCHMARK" = true ]; then
    print_status "Workload running on node 1. Monitoring progress..."
    
    # Wait for node 1 to complete or fail
    wait ${NODE_PIDS[1]} 2>/dev/null || true
    
    print_status "Workload completed. Cluster will remain running."
    print_status "Press Ctrl+C to stop the cluster."
else
    print_status "Cluster is running. Press Ctrl+C to stop all nodes."
fi

# Keep script running until interrupted
while true; do
    sleep 5
    
    # Check if all nodes are still running
    failed_nodes=0
    for i in $(seq 1 "$NUM_NODES"); do
        pid=${NODE_PIDS[$i]}
        if ! kill -0 "$pid" 2>/dev/null; then
            failed_nodes=$((failed_nodes + 1))
        fi
    done
    
    if [ "$failed_nodes" -gt 0 ]; then
        print_warning "$failed_nodes node(s) have failed"
        
        if [ "$failed_nodes" -gt $((NUM_NODES / 2)) ]; then
            print_error "Majority of nodes failed. Stopping cluster."
            break
        fi
    fi
done 