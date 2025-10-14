#include "core/node.h"
#include "protocols/hybrid_protocol.h"
#include "network/network_manager.h"
#include "performance/metrics.h"
#include "utils/logger.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <signal.h>
#include <sstream>

using namespace replication;

// Global flag for graceful shutdown
volatile bool g_running = true;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nReceived shutdown signal. Stopping gracefully..." << std::endl;
        g_running = false;
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "Options:\n"
              << "  --node-id ID        Node identifier (required)\n"
              << "  --port PORT         Listen port (default: 8080)\n"
              << "  --peers PEER_LIST   Comma-separated list of peer node IDs\n"
              << "  --mode MODE         Replication mode: chain, quorum, hybrid (default: hybrid)\n"
              << "  --log-level LEVEL   Log level: debug, info, warn, error (default: info)\n"
              << "  --log-file FILE     Log file path (optional)\n"
              << "  --demo              Run demo workload\n"
              << "  --benchmark         Run performance benchmark\n"
              << "  --help              Show this help message\n"
              << std::endl;
}

struct Config {
    uint32_t node_id = 0;
    uint16_t port = 8080;
    std::vector<uint32_t> peers;
    ReplicationMode mode = ReplicationMode::HYBRID_AUTO;
    LogLevel log_level = LogLevel::INFO;
    std::string log_file;
    bool run_demo = false;
    bool run_benchmark = false;
};

bool parse_args(int argc, char* argv[], Config& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            print_usage(argv[0]);
            return false;
        } else if (arg == "--node-id" && i + 1 < argc) {
            config.node_id = std::stoul(argv[++i]);
        } else if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if (arg == "--peers" && i + 1 < argc) {
            std::string peers_str = argv[++i];
            std::stringstream ss(peers_str);
            std::string peer;
            while (std::getline(ss, peer, ',')) {
                config.peers.push_back(std::stoul(peer));
            }
        } else if (arg == "--mode" && i + 1 < argc) {
            std::string mode_str = argv[++i];
            if (mode_str == "chain") {
                config.mode = ReplicationMode::CHAIN_ONLY;
            } else if (mode_str == "quorum") {
                config.mode = ReplicationMode::QUORUM_ONLY;
            } else if (mode_str == "hybrid") {
                config.mode = ReplicationMode::HYBRID_AUTO;
            }
        } else if (arg == "--log-level" && i + 1 < argc) {
            std::string level_str = argv[++i];
            if (level_str == "debug") config.log_level = LogLevel::DEBUG;
            else if (level_str == "info") config.log_level = LogLevel::INFO;
            else if (level_str == "warn") config.log_level = LogLevel::WARNING;
            else if (level_str == "error") config.log_level = LogLevel::ERROR;
        } else if (arg == "--log-file" && i + 1 < argc) {
            config.log_file = argv[++i];
        } else if (arg == "--demo") {
            config.run_demo = true;
        } else if (arg == "--benchmark") {
            config.run_benchmark = true;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return false;
        }
    }
    
    if (config.node_id == 0) {
        std::cerr << "Error: --node-id is required" << std::endl;
        return false;
    }
    
    return true;
}

void run_demo_workload(std::shared_ptr<HybridProtocol> protocol) {
    std::cout << "Starting demo workload..." << std::endl;
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> key_dist(1, 1000);
    std::uniform_int_distribution<> value_dist(1, 10000);
    std::uniform_real_distribution<> op_dist(0.0, 1.0);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    int operations = 0;
    int successful_ops = 0;
    
    while (g_running && operations < 1000) {
        Message request;
        Message response;
        
        // 70% reads, 30% writes
        if (op_dist(gen) < 0.7) {
            // Read operation
            request.type = MessageType::READ_REQUEST;
            request.key = "key_" + std::to_string(key_dist(gen));
            
            if (protocol->process_read(request, response)) {
                successful_ops++;
            }
        } else {
            // Write operation
            request.type = MessageType::WRITE_REQUEST;
            request.key = "key_" + std::to_string(key_dist(gen));
            request.value = "value_" + std::to_string(value_dist(gen));
            
            if (protocol->process_write(request, response)) {
                successful_ops++;
            }
        }
        
        operations++;
        
        // Print progress every 100 operations
        if (operations % 100 == 0) {
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - start_time).count();
            double throughput = operations / (elapsed / 1000.0);
            
            std::cout << "Progress: " << operations << "/1000 operations, "
                      << "Success rate: " << (successful_ops * 100.0 / operations) << "%, "
                      << "Throughput: " << throughput << " ops/sec" << std::endl;
        }
        
        // Sleep briefly to avoid overwhelming the system
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    std::cout << "\nDemo completed!" << std::endl;
    std::cout << "Total operations: " << operations << std::endl;
    std::cout << "Successful operations: " << successful_ops << std::endl;
    std::cout << "Success rate: " << (successful_ops * 100.0 / operations) << "%" << std::endl;
    std::cout << "Total time: " << total_elapsed << "ms" << std::endl;
    std::cout << "Average throughput: " << (operations / (total_elapsed / 1000.0)) << " ops/sec" << std::endl;
    
    // Print protocol-specific metrics
    AdaptiveMetrics metrics = protocol->get_current_metrics();
    std::cout << "Read/Write ratio: " << metrics.read_write_ratio << std::endl;
    std::cout << "Average latency: " << metrics.average_latency << "ms" << std::endl;
    std::cout << "Hybrid efficiency: " << protocol->get_hybrid_efficiency() << std::endl;
}

void run_benchmark(std::shared_ptr<HybridProtocol> protocol) {
    std::cout << "Starting performance benchmark..." << std::endl;
    
    // Initialize performance monitor
    if (!g_performance_monitor) {
        g_performance_monitor = std::make_unique<PerformanceMonitor>();
    }
    
    std::vector<std::thread> worker_threads;
    std::atomic<int> completed_operations(0);
    constexpr int num_threads = 4;
    constexpr int ops_per_thread = 250;
    
    auto benchmark_worker = [&](int thread_id) {
        std::random_device rd;
        std::mt19937 gen(rd() + thread_id);
        std::uniform_int_distribution<> key_dist(1, 1000);
        std::uniform_int_distribution<> value_dist(1, 10000);
        std::uniform_real_distribution<> op_dist(0.0, 1.0);
        
        for (int i = 0; i < ops_per_thread && g_running; ++i) {
            uint64_t op_id = thread_id * ops_per_thread + i;
            Message request;
            Message response;
            
            if (op_dist(gen) < 0.7) {
                // Read operation
                request.type = MessageType::READ_REQUEST;
                request.key = "bench_key_" + std::to_string(key_dist(gen));
                
                TRACK_OPERATION(op_id, MessageType::READ_REQUEST, request.key);
                bool success = protocol->process_read(request, response);
                END_OPERATION(op_id, success, ReplicationMode::HYBRID_AUTO, 1);
            } else {
                // Write operation
                request.type = MessageType::WRITE_REQUEST;
                request.key = "bench_key_" + std::to_string(key_dist(gen));
                request.value = "bench_value_" + std::to_string(value_dist(gen));
                
                TRACK_OPERATION(op_id, MessageType::WRITE_REQUEST, request.key);
                bool success = protocol->process_write(request, response);
                END_OPERATION(op_id, success, ReplicationMode::HYBRID_AUTO, 1);
            }
            
            completed_operations.fetch_add(1);
            
            // Small delay to simulate realistic load
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    };
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Start worker threads
    for (int i = 0; i < num_threads; ++i) {
        worker_threads.emplace_back(benchmark_worker, i);
    }
    
    // Monitor progress
    while (completed_operations.load() < num_threads * ops_per_thread && g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        int completed = completed_operations.load();
        auto current_time = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - start_time).count();
        
        if (elapsed > 0) {
            double throughput = completed / (elapsed / 1000.0);
            std::cout << "Benchmark progress: " << completed << "/" << (num_threads * ops_per_thread)
                      << " operations, Throughput: " << throughput << " ops/sec" << std::endl;
        }
    }
    
    // Wait for all threads to complete
    for (auto& thread : worker_threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    // Print benchmark results
    std::cout << "\nBenchmark completed!" << std::endl;
    
    if (g_performance_monitor) {
        PerformanceStats stats = g_performance_monitor->get_current_stats();
        
        std::cout << "=== Performance Results ===" << std::endl;
        std::cout << "Total operations: " << completed_operations.load() << std::endl;
        std::cout << "Total time: " << total_elapsed << "ms" << std::endl;
        std::cout << "Throughput: " << stats.throughput_ops_per_sec << " ops/sec" << std::endl;
        std::cout << "Average latency: " << stats.average_latency_ms << "ms" << std::endl;
        std::cout << "95th percentile latency: " << stats.p95_latency_ms << "ms" << std::endl;
        std::cout << "99th percentile latency: " << stats.p99_latency_ms << "ms" << std::endl;
        std::cout << "Success rate: " << (stats.success_rate * 100) << "%" << std::endl;
        std::cout << "CPU utilization: " << stats.cpu_utilization << "%" << std::endl;
        std::cout << "Memory usage: " << stats.memory_usage_mb << "MB" << std::endl;
        
        // Protocol-specific metrics
        std::cout << "\n=== Protocol Performance ===" << std::endl;
        std::cout << "Hybrid efficiency: " << protocol->get_hybrid_efficiency() << std::endl;
        std::cout << "Mode switching overhead: " << protocol->get_mode_switching_overhead() << "ms" << std::endl;
        
        AdaptiveMetrics adaptive_metrics = protocol->get_current_metrics();
        std::cout << "Read/Write ratio: " << adaptive_metrics.read_write_ratio << std::endl;
        std::cout << "Workload pattern: " << static_cast<int>(adaptive_metrics.pattern) << std::endl;
        
        // Export detailed metrics
        g_performance_monitor->export_metrics_to_file("benchmark_results.csv");
        std::cout << "Detailed metrics exported to benchmark_results.csv" << std::endl;
        
        // Performance recommendations
        auto recommendations = g_performance_monitor->get_performance_recommendations();
        if (!recommendations.empty()) {
            std::cout << "\n=== Performance Recommendations ===" << std::endl;
            for (const auto& rec : recommendations) {
                std::cout << "- " << rec << std::endl;
            }
        }
    }
}

void print_status(std::shared_ptr<HybridProtocol> protocol) {
    if (g_performance_monitor) {
        PerformanceStats stats = g_performance_monitor->get_current_stats();
        AdaptiveMetrics metrics = protocol->get_current_metrics();
        
        std::cout << "\n=== System Status ===" << std::endl;
        std::cout << "Throughput: " << stats.throughput_ops_per_sec << " ops/sec" << std::endl;
        std::cout << "Average latency: " << stats.average_latency_ms << "ms" << std::endl;
        std::cout << "Success rate: " << (stats.success_rate * 100) << "%" << std::endl;
        std::cout << "Hybrid efficiency: " << protocol->get_hybrid_efficiency() << std::endl;
        std::cout << "Active nodes: " << metrics.active_nodes << std::endl;
        
        // Check for alerts
        if (g_performance_monitor->has_performance_alerts()) {
            auto alerts = g_performance_monitor->get_active_alerts();
            std::cout << "\n⚠️  Active Alerts:" << std::endl;
            for (const auto& alert : alerts) {
                std::cout << "  " << alert << std::endl;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Parse command line arguments
    Config config;
    if (!parse_args(argc, argv, config)) {
        return 1;
    }
    
    // Initialize logging
    Logger& logger = Logger::getInstance();
    logger.setLogLevel(config.log_level);
    if (!config.log_file.empty()) {
        logger.setLogFile(config.log_file);
    }
    
    LOG_INFO("Starting Hybrid Chain-Quorum Replication Node " + std::to_string(config.node_id));
    
    try {
        // Create cluster node list
        std::vector<uint32_t> cluster_nodes = config.peers;
        cluster_nodes.push_back(config.node_id);
        std::sort(cluster_nodes.begin(), cluster_nodes.end());
        
        // Initialize performance monitoring
        g_performance_monitor = std::make_unique<PerformanceMonitor>();
        
        // Create node
        auto node = std::make_shared<Node>(config.node_id, cluster_nodes);
        
        // Create network manager
        auto network_manager = std::make_unique<NetworkManager>(config.node_id, config.port);
        
        // Initialize hybrid protocol
        std::vector<uint32_t> chain_order = cluster_nodes;
        std::vector<uint32_t> quorum_nodes = cluster_nodes;
        
        auto hybrid_protocol = std::make_shared<HybridProtocol>(node, chain_order, quorum_nodes);
        hybrid_protocol->set_read_preference(config.mode);
        hybrid_protocol->set_write_preference(config.mode);
        
        // Enable optimizations
        hybrid_protocol->enable_intelligent_routing(true);
        hybrid_protocol->enable_load_balancing(true);
        hybrid_protocol->enable_caching(true);
        hybrid_protocol->enable_request_batching(true);
        
        // Start services
        if (!node->start()) {
            LOG_ERROR("Failed to start node");
            return 1;
        }
        
        if (!network_manager->start()) {
            LOG_ERROR("Failed to start network manager");
            return 1;
        }
        
        // Add peer nodes to network manager
        for (uint32_t peer_id : config.peers) {
            // In a real implementation, these would be actual hostnames/IPs
            std::string hostname = "127.0.0.1";
            uint16_t peer_port = 8080 + peer_id; // Simple port assignment
            network_manager->add_node(peer_id, hostname, peer_port);
        }
        
        // Start heartbeat
        network_manager->start_heartbeat(30000); // 30 seconds
        
        LOG_INFO("Node started successfully. Listening on port " + std::to_string(config.port));
        
        // Run demo or benchmark if requested
        if (config.run_demo) {
            run_demo_workload(hybrid_protocol);
        } else if (config.run_benchmark) {
            run_benchmark(hybrid_protocol);
        } else {
            // Interactive mode
            std::cout << "Node " << config.node_id << " is running. Type 'help' for commands." << std::endl;
            
            std::string command;
            while (g_running && std::getline(std::cin, command)) {
                if (command == "help") {
                    std::cout << "Available commands:\n"
                              << "  status    - Show system status\n"
                              << "  metrics   - Export performance metrics\n"
                              << "  reset     - Reset performance counters\n"
                              << "  demo      - Run demo workload\n"
                              << "  benchmark - Run performance benchmark\n"
                              << "  quit      - Exit the program\n" << std::endl;
                } else if (command == "status") {
                    print_status(hybrid_protocol);
                } else if (command == "metrics") {
                    if (g_performance_monitor) {
                        g_performance_monitor->export_metrics_to_file("metrics_export.csv");
                        std::cout << "Metrics exported to metrics_export.csv" << std::endl;
                    }
                } else if (command == "reset") {
                    if (g_performance_monitor) {
                        g_performance_monitor->reset_metrics();
                        std::cout << "Performance metrics reset" << std::endl;
                    }
                } else if (command == "demo") {
                    run_demo_workload(hybrid_protocol);
                } else if (command == "benchmark") {
                    run_benchmark(hybrid_protocol);
                } else if (command == "quit" || command == "exit") {
                    break;
                } else if (!command.empty()) {
                    std::cout << "Unknown command: " << command << ". Type 'help' for available commands." << std::endl;
                }
            }
        }
        
        LOG_INFO("Shutting down node " + std::to_string(config.node_id));
        
        // Clean shutdown
        network_manager->stop();
        node->stop();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Fatal error: " + std::string(e.what()));
        return 1;
    }
    
    LOG_INFO("Node " + std::to_string(config.node_id) + " shut down successfully");
    return 0;
} 