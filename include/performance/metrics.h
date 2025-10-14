#pragma once

#include "../core/message.h"
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <unordered_map>

namespace replication {

struct PerformanceStats {
    double throughput_ops_per_sec;
    double average_latency_ms;
    double p95_latency_ms;
    double p99_latency_ms;
    double success_rate;
    double cpu_utilization;
    double memory_usage_mb;
    double network_utilization;
    
    PerformanceStats() : throughput_ops_per_sec(0.0), average_latency_ms(0.0),
                        p95_latency_ms(0.0), p99_latency_ms(0.0), success_rate(0.0),
                        cpu_utilization(0.0), memory_usage_mb(0.0), network_utilization(0.0) {}
};

struct OperationMetrics {
    uint64_t start_time;
    uint64_t end_time;
    MessageType operation_type;
    bool success;
    std::string key;
    size_t value_size;
    uint32_t hops;
    ReplicationMode mode_used;
    
    uint64_t get_latency_ms() const {
        return (end_time - start_time) / 1000; // Convert microseconds to milliseconds
    }
};

class PerformanceMonitor {
public:
    PerformanceMonitor();
    ~PerformanceMonitor() = default;
    
    // Operation tracking
    void start_operation(uint64_t operation_id, MessageType type, const std::string& key);
    void end_operation(uint64_t operation_id, bool success, ReplicationMode mode, uint32_t hops = 1);
    
    // Real-time metrics
    PerformanceStats get_current_stats() const;
    PerformanceStats get_historical_stats(uint64_t duration_ms) const;
    
    // Detailed analytics
    double get_throughput() const;
    double get_average_latency() const;
    double get_percentile_latency(double percentile) const;
    double get_success_rate() const;
    
    // Mode-specific metrics
    PerformanceStats get_chain_stats() const;
    PerformanceStats get_quorum_stats() const;
    PerformanceStats get_hybrid_stats() const;
    
    // System resource monitoring
    void update_system_stats();
    double get_cpu_utilization() const { return cpu_utilization_.load(); }
    double get_memory_usage() const { return memory_usage_.load(); }
    double get_network_utilization() const { return network_utilization_.load(); }
    
    // Performance optimization insights
    std::vector<std::string> get_performance_recommendations() const;
    bool should_scale_up() const;
    bool should_scale_down() const;
    ReplicationMode get_recommended_mode() const;
    
    // Historical analysis
    void enable_detailed_logging(bool enable) { detailed_logging_enabled_ = enable; }
    void export_metrics_to_file(const std::string& filename) const;
    void reset_metrics();
    
    // Alerting
    void set_latency_threshold(double threshold_ms) { latency_threshold_ = threshold_ms; }
    void set_throughput_threshold(double threshold_ops) { throughput_threshold_ = threshold_ops; }
    bool has_performance_alerts() const;
    std::vector<std::string> get_active_alerts() const;

private:
    // Operation tracking
    mutable std::mutex operations_mutex_;
    std::unordered_map<uint64_t, OperationMetrics> active_operations_;
    std::vector<OperationMetrics> completed_operations_;
    
    // Real-time counters
    std::atomic<uint64_t> total_operations_;
    std::atomic<uint64_t> successful_operations_;
    std::atomic<uint64_t> failed_operations_;
    std::atomic<double> cumulative_latency_;
    
    // System resources
    std::atomic<double> cpu_utilization_;
    std::atomic<double> memory_usage_;
    std::atomic<double> network_utilization_;
    
    // Mode-specific tracking
    std::atomic<uint64_t> chain_operations_;
    std::atomic<uint64_t> quorum_operations_;
    std::atomic<uint64_t> hybrid_operations_;
    std::atomic<double> chain_latency_;
    std::atomic<double> quorum_latency_;
    std::atomic<double> hybrid_latency_;
    
    // Configuration
    bool detailed_logging_enabled_;
    double latency_threshold_;
    double throughput_threshold_;
    
    // Time tracking
    uint64_t start_time_;
    
    // Helper methods
    uint64_t get_current_timestamp() const;
    std::vector<uint64_t> get_latency_samples(uint64_t duration_ms) const;
    double calculate_percentile(const std::vector<uint64_t>& sorted_values, double percentile) const;
    void cleanup_old_operations();
    
    // System monitoring
    double measure_cpu_usage() const;
    double measure_memory_usage() const;
    double measure_network_usage() const;
    
    // Analysis helpers
    bool is_performance_degraded() const;
    ReplicationMode analyze_optimal_mode() const;
    double calculate_efficiency_score(ReplicationMode mode) const;
};

// Global performance monitor instance
extern std::unique_ptr<PerformanceMonitor> g_performance_monitor;

// Convenience macros for performance tracking
#define TRACK_OPERATION(id, type, key) \
    if (g_performance_monitor) g_performance_monitor->start_operation(id, type, key)

#define END_OPERATION(id, success, mode, hops) \
    if (g_performance_monitor) g_performance_monitor->end_operation(id, success, mode, hops)

} // namespace replication 