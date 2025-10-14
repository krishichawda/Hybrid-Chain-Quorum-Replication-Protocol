#include "performance/metrics.h"
#include "protocols/hybrid_protocol.h"
#include "core/node.h"
#include "utils/logger.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <random>

using namespace replication;

class PerformanceTest {
public:
    PerformanceTest() {
        Logger::getInstance().setLogLevel(LogLevel::WARNING);
        g_performance_monitor = std::make_unique<PerformanceMonitor>();
    }
    
    void run_all_tests() {
        std::cout << "Running Performance Tests..." << std::endl;
        
        test_performance_monitor_basic();
        test_throughput_measurement();
        test_latency_measurement();
        test_percentile_calculations();
        test_metrics_export();
        test_alerting_system();
        test_system_resource_monitoring();
        test_protocol_comparison();
        test_scalability_limits();
        
        std::cout << "All Performance tests passed!" << std::endl;
    }

private:
    void test_performance_monitor_basic() {
        std::cout << "  Testing performance monitor basic functionality..." << std::endl;
        
        g_performance_monitor->reset_metrics();
        
        // Test operation tracking
        for (uint64_t i = 1; i <= 100; ++i) {
            g_performance_monitor->start_operation(i, MessageType::READ_REQUEST, "test_key_" + std::to_string(i));
            
            // Simulate some processing time
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            
            bool success = (i % 10 != 0); // 90% success rate
            g_performance_monitor->end_operation(i, success, ReplicationMode::HYBRID_AUTO, 1);
        }
        
        PerformanceStats stats = g_performance_monitor->get_current_stats();
        
        assert(stats.success_rate >= 0.85 && stats.success_rate <= 0.95); // ~90%
        assert(stats.throughput_ops_per_sec > 0);
        assert(stats.average_latency_ms >= 0);
        
        std::cout << "    ✓ Performance monitor basic test passed" << std::endl;
    }
    
    void test_throughput_measurement() {
        std::cout << "  Testing throughput measurement..." << std::endl;
        
        g_performance_monitor->reset_metrics();
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Simulate high-throughput operations
        std::vector<std::thread> workers;
        std::atomic<uint64_t> op_counter(1);
        
        for (int t = 0; t < 4; ++t) {
            workers.emplace_back([&]() {
                for (int i = 0; i < 250; ++i) {
                    uint64_t op_id = op_counter.fetch_add(1);
                    
                    g_performance_monitor->start_operation(op_id, MessageType::WRITE_REQUEST, 
                                                         "throughput_key_" + std::to_string(op_id));
                    
                    // Very fast operation
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                    
                    g_performance_monitor->end_operation(op_id, true, ReplicationMode::CHAIN_ONLY, 1);
                }
            });
        }
        
        for (auto& worker : workers) {
            worker.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        double measured_throughput = g_performance_monitor->get_throughput();
        double expected_throughput = 1000.0 / (duration.count() / 1000.0); // ops/sec
        
        // Measured throughput should be reasonably close to expected
        assert(measured_throughput > expected_throughput * 0.5);
        assert(measured_throughput < expected_throughput * 2.0);
        
        std::cout << "    ✓ Throughput measurement test passed (measured: " 
                  << std::fixed << std::setprecision(0) << measured_throughput << " ops/sec)" << std::endl;
    }
    
    void test_latency_measurement() {
        std::cout << "  Testing latency measurement..." << std::endl;
        
        g_performance_monitor->reset_metrics();
        
        // Test different latency scenarios
        std::vector<uint64_t> expected_latencies = {1000, 5000, 10000, 2000, 8000}; // microseconds
        
        for (size_t i = 0; i < expected_latencies.size(); ++i) {
            uint64_t op_id = i + 1;
            g_performance_monitor->start_operation(op_id, MessageType::READ_REQUEST, "latency_key");
            
            // Simulate the expected latency
            std::this_thread::sleep_for(std::chrono::microseconds(expected_latencies[i]));
            
            g_performance_monitor->end_operation(op_id, true, ReplicationMode::QUORUM_ONLY, 1);
        }
        
        double avg_latency = g_performance_monitor->get_average_latency();
        double expected_avg = 5200.0; // (1+5+10+2+8) * 1000 / 5 = 5.2ms
        
        // Allow for some timing variance
        assert(avg_latency >= expected_avg * 0.8);
        assert(avg_latency <= expected_avg * 1.2);
        
        std::cout << "    ✓ Latency measurement test passed (measured: " 
                  << std::fixed << std::setprecision(2) << avg_latency << "ms)" << std::endl;
    }
    
    void test_percentile_calculations() {
        std::cout << "  Testing percentile calculations..." << std::endl;
        
        g_performance_monitor->reset_metrics();
        
        // Generate operations with known latency distribution
        std::vector<uint64_t> latencies;
        for (int i = 1; i <= 100; ++i) {
            latencies.push_back(i * 1000); // 1ms to 100ms
        }
        
        // Add operations in random order
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(latencies.begin(), latencies.end(), gen);
        
        for (size_t i = 0; i < latencies.size(); ++i) {
            uint64_t op_id = i + 1;
            g_performance_monitor->start_operation(op_id, MessageType::READ_REQUEST, "percentile_key");
            
            std::this_thread::sleep_for(std::chrono::microseconds(latencies[i]));
            
            g_performance_monitor->end_operation(op_id, true, ReplicationMode::HYBRID_AUTO, 1);
        }
        
        double p95 = g_performance_monitor->get_percentile_latency(0.95);
        double p99 = g_performance_monitor->get_percentile_latency(0.99);
        
        // For latencies 1-100ms, 95th percentile should be around 95ms, 99th around 99ms
        assert(p95 >= 90.0 && p95 <= 100.0);
        assert(p99 >= 95.0 && p99 <= 105.0);
        assert(p99 > p95); // 99th percentile should be higher than 95th
        
        std::cout << "    ✓ Percentile calculations test passed (P95: " 
                  << std::fixed << std::setprecision(1) << p95 << "ms, P99: " << p99 << "ms)" << std::endl;
    }
    
    void test_metrics_export() {
        std::cout << "  Testing metrics export..." << std::endl;
        
        g_performance_monitor->reset_metrics();
        
        // Generate some test data
        for (int i = 1; i <= 50; ++i) {
            g_performance_monitor->start_operation(i, MessageType::WRITE_REQUEST, "export_key_" + std::to_string(i));
            
            std::this_thread::sleep_for(std::chrono::microseconds(i * 100));
            
            bool success = (i % 5 != 0); // 80% success rate
            g_performance_monitor->end_operation(i, success, ReplicationMode::CHAIN_ONLY, i % 3 + 1);
        }
        
        // Export metrics
        std::string export_file = "test_metrics_export.csv";
        g_performance_monitor->export_metrics_to_file(export_file);
        
        // Verify file was created (basic check)
        std::ifstream file(export_file);
        assert(file.is_open());
        
        std::string line;
        std::getline(file, line); // Header line
        assert(line.find("timestamp") != std::string::npos);
        assert(line.find("operation_type") != std::string::npos);
        assert(line.find("success") != std::string::npos);
        assert(line.find("latency_ms") != std::string::npos);
        
        int data_lines = 0;
        while (std::getline(file, line)) {
            data_lines++;
        }
        
        assert(data_lines == 50); // Should have 50 data lines
        
        file.close();
        std::remove(export_file.c_str()); // Cleanup
        
        std::cout << "    ✓ Metrics export test passed" << std::endl;
    }
    
    void test_alerting_system() {
        std::cout << "  Testing alerting system..." << std::endl;
        
        g_performance_monitor->reset_metrics();
        
        // Set thresholds
        g_performance_monitor->set_latency_threshold(50.0); // 50ms
        g_performance_monitor->set_throughput_threshold(100.0); // 100 ops/sec
        
        // Generate operations that should trigger alerts
        for (int i = 1; i <= 10; ++i) {
            g_performance_monitor->start_operation(i, MessageType::READ_REQUEST, "alert_key");
            
            // High latency operations
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 100ms
            
            g_performance_monitor->end_operation(i, true, ReplicationMode::QUORUM_ONLY, 1);
        }
        
        // Check if alerts are triggered
        bool has_alerts = g_performance_monitor->has_performance_alerts();
        assert(has_alerts == true);
        
        std::vector<std::string> alerts = g_performance_monitor->get_active_alerts();
        assert(!alerts.empty());
        
        // Should have high latency alert
        bool found_latency_alert = false;
        for (const auto& alert : alerts) {
            if (alert.find("HIGH_LATENCY") != std::string::npos) {
                found_latency_alert = true;
                break;
            }
        }
        assert(found_latency_alert == true);
        
        std::cout << "    ✓ Alerting system test passed (" << alerts.size() << " alerts triggered)" << std::endl;
    }
    
    void test_system_resource_monitoring() {
        std::cout << "  Testing system resource monitoring..." << std::endl;
        
        g_performance_monitor->update_system_stats();
        
        // Check that system stats are reasonable
        double cpu = g_performance_monitor->get_cpu_utilization();
        double memory = g_performance_monitor->get_memory_usage();
        double network = g_performance_monitor->get_network_utilization();
        
        assert(cpu >= 0.0 && cpu <= 100.0);
        assert(memory >= 0.0);
        assert(network >= 0.0 && network <= 100.0);
        
        // Test scaling recommendations
        bool should_scale_up = g_performance_monitor->should_scale_up();
        bool should_scale_down = g_performance_monitor->should_scale_down();
        
        // Both shouldn't be true at the same time
        assert(!(should_scale_up && should_scale_down));
        
        std::cout << "    ✓ System resource monitoring test passed" << std::endl;
    }
    
    void test_protocol_comparison() {
        std::cout << "  Testing protocol comparison..." << std::endl;
        
        g_performance_monitor->reset_metrics();
        
        // Simulate operations for different protocols
        
        // Chain operations
        for (int i = 1; i <= 30; ++i) {
            g_performance_monitor->start_operation(i, MessageType::READ_REQUEST, "chain_key");
            std::this_thread::sleep_for(std::chrono::microseconds(2000)); // 2ms
            g_performance_monitor->end_operation(i, true, ReplicationMode::CHAIN_ONLY, 1);
        }
        
        // Quorum operations  
        for (int i = 31; i <= 60; ++i) {
            g_performance_monitor->start_operation(i, MessageType::WRITE_REQUEST, "quorum_key");
            std::this_thread::sleep_for(std::chrono::microseconds(5000)); // 5ms
            g_performance_monitor->end_operation(i, true, ReplicationMode::QUORUM_ONLY, 1);
        }
        
        // Hybrid operations
        for (int i = 61; i <= 90; ++i) {
            g_performance_monitor->start_operation(i, MessageType::READ_REQUEST, "hybrid_key");
            std::this_thread::sleep_for(std::chrono::microseconds(3000)); // 3ms
            g_performance_monitor->end_operation(i, true, ReplicationMode::HYBRID_AUTO, 1);
        }
        
        // Compare protocol performance
        PerformanceStats chain_stats = g_performance_monitor->get_chain_stats();
        PerformanceStats quorum_stats = g_performance_monitor->get_quorum_stats();
        PerformanceStats hybrid_stats = g_performance_monitor->get_hybrid_stats();
        
        // Chain should be fastest for reads
        assert(chain_stats.average_latency_ms < quorum_stats.average_latency_ms);
        
        // Hybrid should be between chain and quorum
        assert(hybrid_stats.average_latency_ms > chain_stats.average_latency_ms);
        assert(hybrid_stats.average_latency_ms < quorum_stats.average_latency_ms);
        
        ReplicationMode recommended = g_performance_monitor->get_recommended_mode();
        assert(recommended == ReplicationMode::CHAIN_ONLY || 
               recommended == ReplicationMode::QUORUM_ONLY || 
               recommended == ReplicationMode::HYBRID_AUTO);
        
        std::cout << "    ✓ Protocol comparison test passed" << std::endl;
    }
    
    void test_scalability_limits() {
        std::cout << "  Testing scalability limits..." << std::endl;
        
        std::vector<uint32_t> nodes = {1, 2, 3, 4, 5};
        auto node = std::make_shared<Node>(1, nodes);
        node->start();
        
        HybridProtocol hybrid(node, nodes, nodes);
        
        g_performance_monitor->reset_metrics();
        
        // Test with increasing load
        std::vector<int> load_levels = {10, 50, 100, 200};
        std::vector<double> throughputs;
        
        for (int load : load_levels) {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            // Generate load
            std::vector<std::thread> workers;
            std::atomic<int> completed_ops(0);
            
            for (int t = 0; t < 4; ++t) {
                workers.emplace_back([&, t, load]() {
                    for (int i = 0; i < load / 4; ++i) {
                        uint64_t op_id = t * (load / 4) + i + 1;
                        
                        Message request;
                        request.type = MessageType::READ_REQUEST;
                        request.key = "scale_key_" + std::to_string(op_id);
                        
                        TRACK_OPERATION(op_id, MessageType::READ_REQUEST, request.key);
                        
                        Message response;
                        bool success = hybrid.process_read(request, response);
                        
                        END_OPERATION(op_id, success, ReplicationMode::HYBRID_AUTO, 1);
                        completed_ops.fetch_add(1);
                    }
                });
            }
            
            for (auto& worker : workers) {
                worker.join();
            }
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            
            double throughput = completed_ops.load() / (duration.count() / 1000.0);
            throughputs.push_back(throughput);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Brief pause between tests
        }
        
        // Verify throughput scaling
        assert(throughputs.size() == load_levels.size());
        
        // Should generally increase with load (up to a point)
        assert(throughputs[1] > throughputs[0] * 0.8); // Allow some variance
        
        node->stop();
        
        std::cout << "    ✓ Scalability limits test passed" << std::endl;
    }
};

void run_performance_tests() {
    try {
        PerformanceTest test;
        test.run_all_tests();
        std::cout << "\n🎉 All Performance tests completed successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Performance test failed: " << e.what() << std::endl;
        throw;
    }
} 