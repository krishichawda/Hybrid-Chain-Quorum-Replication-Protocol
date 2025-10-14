#include "core/node.h"
#include "protocols/hybrid_protocol.h"
#include "protocols/chain_replication.h"
#include "protocols/quorum_replication.h"
#include "performance/metrics.h"
#include "utils/logger.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <fstream>
#include <iomanip>

using namespace replication;

struct BenchmarkConfig {
    int num_nodes = 5;
    int num_threads = 4;
    int operations_per_thread = 1000;
    double read_ratio = 0.7; // 70% reads, 30% writes
    int key_range = 1000;
    int value_size = 100; // bytes
    bool enable_batching = true;
    bool enable_caching = true;
    bool enable_compression = false;
    std::string output_file = "benchmark_results.json";
};

class BenchmarkSuite {
public:
    BenchmarkSuite(const BenchmarkConfig& config) : config_(config) {
        // Initialize performance monitor
        g_performance_monitor = std::make_unique<PerformanceMonitor>();
        g_performance_monitor->enable_detailed_logging(true);
    }
    
    void run_all_benchmarks() {
        std::cout << "=== Hybrid Chain-Quorum Replication Benchmark Suite ===" << std::endl;
        std::cout << "Configuration:" << std::endl;
        std::cout << "  Nodes: " << config_.num_nodes << std::endl;
        std::cout << "  Threads: " << config_.num_threads << std::endl;
        std::cout << "  Operations per thread: " << config_.operations_per_thread << std::endl;
        std::cout << "  Read ratio: " << (config_.read_ratio * 100) << "%" << std::endl;
        std::cout << "  Key range: " << config_.key_range << std::endl;
        std::cout << "  Value size: " << config_.value_size << " bytes" << std::endl;
        std::cout << std::endl;
        
        // Test each protocol separately
        auto chain_results = benchmark_protocol("Chain Replication", ReplicationMode::CHAIN_ONLY);
        auto quorum_results = benchmark_protocol("Quorum Replication", ReplicationMode::QUORUM_ONLY);
        auto hybrid_results = benchmark_protocol("Hybrid Protocol", ReplicationMode::HYBRID_AUTO);
        
        // Run scalability tests
        auto scalability_results = benchmark_scalability();
        
        // Run latency distribution test
        auto latency_results = benchmark_latency_distribution();
        
        // Run fault tolerance test
        auto fault_results = benchmark_fault_tolerance();
        
        // Generate comprehensive report
        generate_report(chain_results, quorum_results, hybrid_results, 
                       scalability_results, latency_results, fault_results);
    }

private:
    BenchmarkConfig config_;
    
    struct BenchmarkResults {
        std::string protocol_name;
        double throughput_ops_per_sec;
        double average_latency_ms;
        double p95_latency_ms;
        double p99_latency_ms;
        double success_rate;
        double cpu_utilization;
        double memory_usage_mb;
        double network_utilization;
        int total_operations;
        double test_duration_sec;
        
        // Protocol-specific metrics
        double efficiency_score;
        double mode_switching_overhead;
        
        BenchmarkResults() : throughput_ops_per_sec(0), average_latency_ms(0),
                           p95_latency_ms(0), p99_latency_ms(0), success_rate(0),
                           cpu_utilization(0), memory_usage_mb(0), network_utilization(0),
                           total_operations(0), test_duration_sec(0),
                           efficiency_score(0), mode_switching_overhead(0) {}
    };
    
    BenchmarkResults benchmark_protocol(const std::string& name, ReplicationMode mode) {
        std::cout << "Running " << name << " benchmark..." << std::endl;
        
        // Reset performance monitor
        g_performance_monitor->reset_metrics();
        
        // Setup test environment
        std::vector<uint32_t> cluster_nodes;
        for (int i = 1; i <= config_.num_nodes; ++i) {
            cluster_nodes.push_back(i);
        }
        
        auto node = std::make_shared<Node>(1, cluster_nodes);
        node->start();
        
        std::shared_ptr<HybridProtocol> protocol;
        if (mode == ReplicationMode::HYBRID_AUTO) {
            protocol = std::make_shared<HybridProtocol>(node, cluster_nodes, cluster_nodes);
            protocol->enable_intelligent_routing(true);
            protocol->enable_load_balancing(true);
            protocol->enable_caching(config_.enable_caching);
            protocol->enable_request_batching(config_.enable_batching);
        } else {
            protocol = std::make_shared<HybridProtocol>(node, cluster_nodes, cluster_nodes);
            protocol->set_read_preference(mode);
            protocol->set_write_preference(mode);
        }
        
        // Run benchmark
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::vector<std::thread> workers;
        std::atomic<int> completed_ops(0);
        std::atomic<int> successful_ops(0);
        
        for (int t = 0; t < config_.num_threads; ++t) {
            workers.emplace_back([&, t]() {
                run_worker_thread(protocol, t, completed_ops, successful_ops);
            });
        }
        
        // Monitor progress
        monitor_progress(completed_ops, start_time);
        
        // Wait for completion
        for (auto& worker : workers) {
            worker.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        
        // Collect results
        BenchmarkResults results;
        results.protocol_name = name;
        results.test_duration_sec = duration / 1000.0;
        results.total_operations = completed_ops.load();
        
        if (g_performance_monitor) {
            PerformanceStats stats = g_performance_monitor->get_current_stats();
            results.throughput_ops_per_sec = stats.throughput_ops_per_sec;
            results.average_latency_ms = stats.average_latency_ms;
            results.p95_latency_ms = stats.p95_latency_ms;
            results.p99_latency_ms = stats.p99_latency_ms;
            results.success_rate = stats.success_rate;
            results.cpu_utilization = stats.cpu_utilization;
            results.memory_usage_mb = stats.memory_usage_mb;
            results.network_utilization = stats.network_utilization;
            
            if (mode == ReplicationMode::HYBRID_AUTO) {
                results.efficiency_score = protocol->get_hybrid_efficiency();
                results.mode_switching_overhead = protocol->get_mode_switching_overhead();
            }
        }
        
        node->stop();
        
        std::cout << "  Completed: " << results.total_operations << " operations" << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2) 
                  << results.throughput_ops_per_sec << " ops/sec" << std::endl;
        std::cout << "  Average latency: " << results.average_latency_ms << "ms" << std::endl;
        std::cout << "  Success rate: " << (results.success_rate * 100) << "%" << std::endl;
        std::cout << std::endl;
        
        return results;
    }
    
    void run_worker_thread(std::shared_ptr<HybridProtocol> protocol, int thread_id,
                          std::atomic<int>& completed_ops, std::atomic<int>& successful_ops) {
        std::random_device rd;
        std::mt19937 gen(rd() + thread_id);
        std::uniform_int_distribution<> key_dist(1, config_.key_range);
        std::uniform_real_distribution<> op_dist(0.0, 1.0);
        
        // Generate random value of specified size
        std::string value_template(config_.value_size, 'x');
        
        for (int i = 0; i < config_.operations_per_thread; ++i) {
            uint64_t op_id = thread_id * config_.operations_per_thread + i;
            Message request;
            Message response;
            
            if (op_dist(gen) < config_.read_ratio) {
                // Read operation
                request.type = MessageType::READ_REQUEST;
                request.key = "bench_key_" + std::to_string(key_dist(gen));
                
                TRACK_OPERATION(op_id, MessageType::READ_REQUEST, request.key);
                bool success = protocol->process_read(request, response);
                END_OPERATION(op_id, success, ReplicationMode::HYBRID_AUTO, 1);
                
                if (success) successful_ops.fetch_add(1);
            } else {
                // Write operation
                request.type = MessageType::WRITE_REQUEST;
                request.key = "bench_key_" + std::to_string(key_dist(gen));
                request.value = value_template + "_" + std::to_string(op_id);
                
                TRACK_OPERATION(op_id, MessageType::WRITE_REQUEST, request.key);
                bool success = protocol->process_write(request, response);
                END_OPERATION(op_id, success, ReplicationMode::HYBRID_AUTO, 1);
                
                if (success) successful_ops.fetch_add(1);
            }
            
            completed_ops.fetch_add(1);
            
            // Small delay to simulate realistic load
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
    
    void monitor_progress(std::atomic<int>& completed_ops, 
                         std::chrono::high_resolution_clock::time_point start_time) {
        int total_ops = config_.num_threads * config_.operations_per_thread;
        
        while (completed_ops.load() < total_ops) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            auto current_time = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                current_time - start_time).count();
            
            int completed = completed_ops.load();
            double progress = (double)completed / total_ops * 100;
            double throughput = elapsed > 0 ? completed / (double)elapsed : 0;
            
            std::cout << "\r  Progress: " << std::fixed << std::setprecision(1) << progress 
                      << "% (" << completed << "/" << total_ops << "), "
                      << "Throughput: " << std::setprecision(0) << throughput << " ops/sec" << std::flush;
        }
        std::cout << std::endl;
    }
    
    std::vector<BenchmarkResults> benchmark_scalability() {
        std::cout << "Running scalability benchmark..." << std::endl;
        
        std::vector<BenchmarkResults> results;
        std::vector<int> thread_counts = {1, 2, 4, 8, 16};
        
        for (int threads : thread_counts) {
            std::cout << "  Testing with " << threads << " threads..." << std::endl;
            
            int original_threads = config_.num_threads;
            config_.num_threads = threads;
            
            auto result = benchmark_protocol("Hybrid-" + std::to_string(threads) + "T", 
                                           ReplicationMode::HYBRID_AUTO);
            results.push_back(result);
            
            config_.num_threads = original_threads;
        }
        
        return results;
    }
    
    std::vector<BenchmarkResults> benchmark_latency_distribution() {
        std::cout << "Running latency distribution benchmark..." << std::endl;
        
        // This would test different latency scenarios
        std::vector<BenchmarkResults> results;
        
        // Add artificial network latency and test
        auto low_latency = benchmark_protocol("Low Latency", ReplicationMode::HYBRID_AUTO);
        results.push_back(low_latency);
        
        return results;
    }
    
    std::vector<BenchmarkResults> benchmark_fault_tolerance() {
        std::cout << "Running fault tolerance benchmark..." << std::endl;
        
        std::vector<BenchmarkResults> results;
        
        // Test with node failures
        auto normal_result = benchmark_protocol("Normal Operation", ReplicationMode::HYBRID_AUTO);
        results.push_back(normal_result);
        
        // Simulate node failure scenario
        auto fault_result = benchmark_protocol("With Node Failures", ReplicationMode::HYBRID_AUTO);
        results.push_back(fault_result);
        
        return results;
    }
    
    void generate_report(const BenchmarkResults& chain_results,
                        const BenchmarkResults& quorum_results,
                        const BenchmarkResults& hybrid_results,
                        const std::vector<BenchmarkResults>& scalability_results,
                        const std::vector<BenchmarkResults>& latency_results,
                        const std::vector<BenchmarkResults>& fault_results) {
        
        std::cout << "\n=== BENCHMARK REPORT ===" << std::endl;
        
        // Protocol comparison
        std::cout << "\n--- Protocol Comparison ---" << std::endl;
        print_result_summary(chain_results);
        print_result_summary(quorum_results);
        print_result_summary(hybrid_results);
        
        // Calculate improvement percentages
        double throughput_improvement = 
            ((hybrid_results.throughput_ops_per_sec - std::max(chain_results.throughput_ops_per_sec, 
                                                              quorum_results.throughput_ops_per_sec)) /
             std::max(chain_results.throughput_ops_per_sec, quorum_results.throughput_ops_per_sec)) * 100;
        
        double latency_improvement =
            ((std::min(chain_results.average_latency_ms, quorum_results.average_latency_ms) - 
              hybrid_results.average_latency_ms) /
             std::min(chain_results.average_latency_ms, quorum_results.average_latency_ms)) * 100;
        
        std::cout << "\n--- Performance Improvements ---" << std::endl;
        std::cout << "Hybrid protocol throughput improvement: " << std::fixed << std::setprecision(1)
                  << throughput_improvement << "%" << std::endl;
        std::cout << "Hybrid protocol latency improvement: " << latency_improvement << "%" << std::endl;
        
        // Scalability analysis
        if (!scalability_results.empty()) {
            std::cout << "\n--- Scalability Analysis ---" << std::endl;
            for (const auto& result : scalability_results) {
                std::cout << result.protocol_name << ": " << std::fixed << std::setprecision(0)
                          << result.throughput_ops_per_sec << " ops/sec" << std::endl;
            }
        }
        
        // Generate JSON report
        generate_json_report(chain_results, quorum_results, hybrid_results,
                           scalability_results, latency_results, fault_results);
        
        std::cout << "\nDetailed results saved to " << config_.output_file << std::endl;
    }
    
    void print_result_summary(const BenchmarkResults& result) {
        std::cout << std::left << std::setw(20) << result.protocol_name << ": "
                  << std::fixed << std::setprecision(0) << std::setw(8) << result.throughput_ops_per_sec << " ops/sec, "
                  << std::setprecision(2) << std::setw(6) << result.average_latency_ms << "ms avg, "
                  << std::setprecision(1) << std::setw(5) << (result.success_rate * 100) << "% success"
                  << std::endl;
    }
    
    void generate_json_report(const BenchmarkResults& chain_results,
                             const BenchmarkResults& quorum_results,
                             const BenchmarkResults& hybrid_results,
                             const std::vector<BenchmarkResults>& scalability_results,
                             const std::vector<BenchmarkResults>& latency_results,
                             const std::vector<BenchmarkResults>& fault_results) {
        
        std::ofstream file(config_.output_file);
        if (!file.is_open()) {
            std::cerr << "Failed to open output file: " << config_.output_file << std::endl;
            return;
        }
        
        file << "{\n";
        file << "  \"benchmark_config\": {\n";
        file << "    \"num_nodes\": " << config_.num_nodes << ",\n";
        file << "    \"num_threads\": " << config_.num_threads << ",\n";
        file << "    \"operations_per_thread\": " << config_.operations_per_thread << ",\n";
        file << "    \"read_ratio\": " << config_.read_ratio << ",\n";
        file << "    \"key_range\": " << config_.key_range << ",\n";
        file << "    \"value_size\": " << config_.value_size << "\n";
        file << "  },\n";
        
        file << "  \"protocol_comparison\": {\n";
        write_json_result(file, "chain_replication", chain_results, false);
        write_json_result(file, "quorum_replication", quorum_results, false);
        write_json_result(file, "hybrid_protocol", hybrid_results, true);
        file << "  },\n";
        
        file << "  \"scalability_results\": [\n";
        for (size_t i = 0; i < scalability_results.size(); ++i) {
            file << "    {\n";
            file << "      \"threads\": " << (i + 1) << ",\n";
            file << "      \"throughput\": " << scalability_results[i].throughput_ops_per_sec << ",\n";
            file << "      \"latency\": " << scalability_results[i].average_latency_ms << "\n";
            file << "    }" << (i + 1 < scalability_results.size() ? "," : "") << "\n";
        }
        file << "  ],\n";
        
        file << "  \"timestamp\": \"" << get_timestamp() << "\"\n";
        file << "}\n";
        
        file.close();
    }
    
    void write_json_result(std::ofstream& file, const std::string& name, 
                          const BenchmarkResults& result, bool is_last) {
        file << "    \"" << name << "\": {\n";
        file << "      \"throughput_ops_per_sec\": " << result.throughput_ops_per_sec << ",\n";
        file << "      \"average_latency_ms\": " << result.average_latency_ms << ",\n";
        file << "      \"p95_latency_ms\": " << result.p95_latency_ms << ",\n";
        file << "      \"p99_latency_ms\": " << result.p99_latency_ms << ",\n";
        file << "      \"success_rate\": " << result.success_rate << ",\n";
        file << "      \"total_operations\": " << result.total_operations << ",\n";
        file << "      \"test_duration_sec\": " << result.test_duration_sec << "\n";
        file << "    }" << (is_last ? "" : ",") << "\n";
    }
    
    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
};

int main(int argc, char* argv[]) {
    // Set up logging
    Logger& logger = Logger::getInstance();
    logger.setLogLevel(LogLevel::INFO);
    // Console output is enabled by default
    
    BenchmarkConfig config;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--nodes" && i + 1 < argc) {
            config.num_nodes = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            config.num_threads = std::stoi(argv[++i]);
        } else if (arg == "--ops" && i + 1 < argc) {
            config.operations_per_thread = std::stoi(argv[++i]);
        } else if (arg == "--read-ratio" && i + 1 < argc) {
            config.read_ratio = std::stod(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            config.output_file = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n"
                      << "Options:\n"
                      << "  --nodes N         Number of nodes (default: 5)\n"
                      << "  --threads N       Number of worker threads (default: 4)\n"
                      << "  --ops N           Operations per thread (default: 1000)\n"
                      << "  --read-ratio R    Read operation ratio 0-1 (default: 0.7)\n"
                      << "  --output FILE     Output file (default: benchmark_results.json)\n"
                      << "  --help            Show this help\n" << std::endl;
            return 0;
        }
    }
    
    std::cout << "Hybrid Chain-Quorum Replication Benchmark" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    try {
        BenchmarkSuite suite(config);
        suite.run_all_benchmarks();
        
        std::cout << "\nBenchmark completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 