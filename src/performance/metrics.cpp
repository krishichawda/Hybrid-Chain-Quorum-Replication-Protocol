#include "performance/metrics.h"
#include "utils/logger.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace replication {

// Global performance monitor instance
std::unique_ptr<PerformanceMonitor> g_performance_monitor = nullptr;

PerformanceMonitor::PerformanceMonitor()
    : total_operations_(0)
    , successful_operations_(0)
    , failed_operations_(0)
    , cumulative_latency_(0.0)
    , cpu_utilization_(0.0)
    , memory_usage_(0.0)
    , network_utilization_(0.0)
    , chain_operations_(0)
    , quorum_operations_(0)
    , hybrid_operations_(0)
    , chain_latency_(0.0)
    , quorum_latency_(0.0)
    , hybrid_latency_(0.0)
    , detailed_logging_enabled_(false)
    , latency_threshold_(100.0) // 100ms
    , throughput_threshold_(1000.0) // 1000 ops/sec
    , start_time_(get_current_timestamp()) {
    
    LOG_INFO("PerformanceMonitor initialized");
}

void PerformanceMonitor::start_operation(uint64_t operation_id, MessageType type, const std::string& key) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    OperationMetrics metrics;
    metrics.start_time = get_current_timestamp();
    metrics.operation_type = type;
    metrics.key = key;
    metrics.success = false;
    metrics.hops = 0;
    metrics.mode_used = ReplicationMode::HYBRID_AUTO;
    
    active_operations_[operation_id] = metrics;
    total_operations_.fetch_add(1);
}

void PerformanceMonitor::end_operation(uint64_t operation_id, bool success, ReplicationMode mode, uint32_t hops) {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    auto it = active_operations_.find(operation_id);
    if (it != active_operations_.end()) {
        it->second.end_time = get_current_timestamp();
        it->second.success = success;
        it->second.mode_used = mode;
        it->second.hops = hops;
        
        // Update counters
        if (success) {
            successful_operations_.fetch_add(1);
        } else {
            failed_operations_.fetch_add(1);
        }
        
        // Update mode-specific metrics
        uint64_t latency = it->second.get_latency_ms();
        switch (mode) {
            case ReplicationMode::CHAIN_ONLY:
                chain_operations_.fetch_add(1);
                chain_latency_.store(chain_latency_.load() + latency);
                break;
            case ReplicationMode::QUORUM_ONLY:
                quorum_operations_.fetch_add(1);
                quorum_latency_.store(quorum_latency_.load() + latency);
                break;
            case ReplicationMode::HYBRID_AUTO:
                hybrid_operations_.fetch_add(1);
                hybrid_latency_.store(hybrid_latency_.load() + latency);
                break;
        }
        
        // Update cumulative latency
        cumulative_latency_.store(cumulative_latency_.load() + latency);
        
        // Move to completed operations
        completed_operations_.push_back(it->second);
        active_operations_.erase(it);
        
        // Keep only recent completed operations (last 10000)
        if (completed_operations_.size() > 10000) {
            completed_operations_.erase(completed_operations_.begin());
        }
        
        if (detailed_logging_enabled_) {
            LOG_DEBUG("Operation " + std::to_string(operation_id) + " completed: " + 
                     (success ? "SUCCESS" : "FAILED") + " in " + std::to_string(latency) + "ms");
        }
    }
}

PerformanceStats PerformanceMonitor::get_current_stats() const {
    PerformanceStats stats;
    
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    uint64_t total_ops = total_operations_.load();
    if (total_ops > 0) {
        // Calculate throughput
        auto current_time = get_current_timestamp();
        auto elapsed_seconds = (current_time - start_time_) / 1000000.0; // Convert to seconds
        if (elapsed_seconds > 0) {
            stats.throughput_ops_per_sec = total_ops / elapsed_seconds;
        }
        
        // Calculate success rate
        stats.success_rate = static_cast<double>(successful_operations_.load()) / total_ops;
        
        // Calculate average latency
        if (successful_operations_.load() > 0) {
            stats.average_latency_ms = cumulative_latency_.load() / successful_operations_.load();
        }
        
        // Calculate percentile latencies
        std::vector<uint64_t> latencies;
        for (const auto& op : completed_operations_) {
            if (op.success) {
                latencies.push_back(op.get_latency_ms());
            }
        }
        
        if (!latencies.empty()) {
            std::sort(latencies.begin(), latencies.end());
            stats.p95_latency_ms = calculate_percentile(latencies, 0.95);
            stats.p99_latency_ms = calculate_percentile(latencies, 0.99);
        }
    }
    
    // System resource metrics
    stats.cpu_utilization = cpu_utilization_.load();
    stats.memory_usage_mb = memory_usage_.load();
    stats.network_utilization = network_utilization_.load();
    
    return stats;
}

PerformanceStats PerformanceMonitor::get_historical_stats(uint64_t duration_ms) const {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    auto cutoff_time = get_current_timestamp() - (duration_ms * 1000); // Convert to microseconds
    
    PerformanceStats stats;
    uint64_t ops_in_window = 0;
    uint64_t successful_ops = 0;
    double total_latency = 0.0;
    std::vector<uint64_t> latencies;
    
    for (const auto& op : completed_operations_) {
        if (op.start_time >= cutoff_time) {
            ops_in_window++;
            if (op.success) {
                successful_ops++;
                uint64_t latency = op.get_latency_ms();
                total_latency += latency;
                latencies.push_back(latency);
            }
        }
    }
    
    if (ops_in_window > 0) {
        stats.throughput_ops_per_sec = ops_in_window / (duration_ms / 1000.0);
        stats.success_rate = static_cast<double>(successful_ops) / ops_in_window;
        
        if (successful_ops > 0) {
            stats.average_latency_ms = total_latency / successful_ops;
            
            std::sort(latencies.begin(), latencies.end());
            stats.p95_latency_ms = calculate_percentile(latencies, 0.95);
            stats.p99_latency_ms = calculate_percentile(latencies, 0.99);
        }
    }
    
    return stats;
}

double PerformanceMonitor::get_throughput() const {
    auto current_time = get_current_timestamp();
    auto elapsed_seconds = (current_time - start_time_) / 1000000.0;
    
    if (elapsed_seconds > 0) {
        return total_operations_.load() / elapsed_seconds;
    }
    
    return 0.0;
}

double PerformanceMonitor::get_average_latency() const {
    uint64_t successful = successful_operations_.load();
    if (successful > 0) {
        return cumulative_latency_.load() / successful;
    }
    return 0.0;
}

double PerformanceMonitor::get_percentile_latency(double percentile) const {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    std::vector<uint64_t> latencies;
    for (const auto& op : completed_operations_) {
        if (op.success) {
            latencies.push_back(op.get_latency_ms());
        }
    }
    
    if (latencies.empty()) {
        return 0.0;
    }
    
    std::sort(latencies.begin(), latencies.end());
    return calculate_percentile(latencies, percentile);
}

double PerformanceMonitor::get_success_rate() const {
    uint64_t total = total_operations_.load();
    if (total > 0) {
        return static_cast<double>(successful_operations_.load()) / total;
    }
    return 0.0;
}

PerformanceStats PerformanceMonitor::get_chain_stats() const {
    PerformanceStats stats;
    
    uint64_t chain_ops = chain_operations_.load();
    if (chain_ops > 0) {
        stats.average_latency_ms = chain_latency_.load() / chain_ops;
        stats.throughput_ops_per_sec = get_throughput(); // Simplified
    }
    
    return stats;
}

PerformanceStats PerformanceMonitor::get_quorum_stats() const {
    PerformanceStats stats;
    
    uint64_t quorum_ops = quorum_operations_.load();
    if (quorum_ops > 0) {
        stats.average_latency_ms = quorum_latency_.load() / quorum_ops;
        stats.throughput_ops_per_sec = get_throughput(); // Simplified
    }
    
    return stats;
}

PerformanceStats PerformanceMonitor::get_hybrid_stats() const {
    PerformanceStats stats;
    
    uint64_t hybrid_ops = hybrid_operations_.load();
    if (hybrid_ops > 0) {
        stats.average_latency_ms = hybrid_latency_.load() / hybrid_ops;
        stats.throughput_ops_per_sec = get_throughput(); // Simplified
    }
    
    return stats;
}

void PerformanceMonitor::update_system_stats() {
    cpu_utilization_.store(measure_cpu_usage());
    memory_usage_.store(measure_memory_usage());
    network_utilization_.store(measure_network_usage());
    
    // Cleanup old operations periodically
    cleanup_old_operations();
}

std::vector<std::string> PerformanceMonitor::get_performance_recommendations() const {
    std::vector<std::string> recommendations;
    
    PerformanceStats current = get_current_stats();
    
    // Latency recommendations
    if (current.average_latency_ms > latency_threshold_) {
        recommendations.push_back("High latency detected (" + 
                                std::to_string(current.average_latency_ms) + 
                                "ms). Consider enabling caching or optimizing network.");
    }
    
    // Throughput recommendations
    if (current.throughput_ops_per_sec < throughput_threshold_) {
        recommendations.push_back("Low throughput detected (" + 
                                std::to_string(current.throughput_ops_per_sec) + 
                                " ops/sec). Consider enabling batching or scaling up.");
    }
    
    // Success rate recommendations
    if (current.success_rate < 0.95) {
        recommendations.push_back("Low success rate (" + 
                                std::to_string(current.success_rate * 100) + 
                                "%). Check network reliability and node health.");
    }
    
    // Mode-specific recommendations
    ReplicationMode recommended = get_recommended_mode();
    recommendations.push_back("Recommended replication mode: " + 
                            std::to_string(static_cast<int>(recommended)));
    
    return recommendations;
}

bool PerformanceMonitor::should_scale_up() const {
    PerformanceStats current = get_current_stats();
    
    return (current.cpu_utilization > 80.0) || 
           (current.memory_usage_mb > 1024.0) || // 1GB threshold
           (current.average_latency_ms > latency_threshold_ * 2);
}

bool PerformanceMonitor::should_scale_down() const {
    PerformanceStats current = get_current_stats();
    
    return (current.cpu_utilization < 20.0) && 
           (current.memory_usage_mb < 256.0) && // 256MB threshold
           (current.average_latency_ms < latency_threshold_ / 2);
}

ReplicationMode PerformanceMonitor::get_recommended_mode() const {
    return analyze_optimal_mode();
}

void PerformanceMonitor::export_metrics_to_file(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open metrics export file: " + filename);
        return;
    }
    
    // Write header
    file << "timestamp,operation_type,success,latency_ms,mode,hops,key\n";
    
    // Write operation data
    for (const auto& op : completed_operations_) {
        file << op.start_time << ","
             << static_cast<int>(op.operation_type) << ","
             << (op.success ? "1" : "0") << ","
             << op.get_latency_ms() << ","
             << static_cast<int>(op.mode_used) << ","
             << op.hops << ","
             << op.key << "\n";
    }
    
    file.close();
    LOG_INFO("Metrics exported to " + filename);
}

void PerformanceMonitor::reset_metrics() {
    std::lock_guard<std::mutex> lock(operations_mutex_);
    
    active_operations_.clear();
    completed_operations_.clear();
    
    total_operations_.store(0);
    successful_operations_.store(0);
    failed_operations_.store(0);
    cumulative_latency_.store(0.0);
    
    chain_operations_.store(0);
    quorum_operations_.store(0);
    hybrid_operations_.store(0);
    chain_latency_.store(0.0);
    quorum_latency_.store(0.0);
    hybrid_latency_.store(0.0);
    
    start_time_ = get_current_timestamp();
    
    LOG_INFO("Performance metrics reset");
}

bool PerformanceMonitor::has_performance_alerts() const {
    PerformanceStats current = get_current_stats();
    
    return (current.average_latency_ms > latency_threshold_) ||
           (current.throughput_ops_per_sec < throughput_threshold_) ||
           (current.success_rate < 0.95) ||
           is_performance_degraded();
}

std::vector<std::string> PerformanceMonitor::get_active_alerts() const {
    std::vector<std::string> alerts;
    PerformanceStats current = get_current_stats();
    
    if (current.average_latency_ms > latency_threshold_) {
        alerts.push_back("HIGH_LATENCY: " + std::to_string(current.average_latency_ms) + "ms");
    }
    
    if (current.throughput_ops_per_sec < throughput_threshold_) {
        alerts.push_back("LOW_THROUGHPUT: " + std::to_string(current.throughput_ops_per_sec) + " ops/sec");
    }
    
    if (current.success_rate < 0.95) {
        alerts.push_back("LOW_SUCCESS_RATE: " + std::to_string(current.success_rate * 100) + "%");
    }
    
    if (current.cpu_utilization > 90.0) {
        alerts.push_back("HIGH_CPU_USAGE: " + std::to_string(current.cpu_utilization) + "%");
    }
    
    if (current.memory_usage_mb > 2048.0) {
        alerts.push_back("HIGH_MEMORY_USAGE: " + std::to_string(current.memory_usage_mb) + "MB");
    }
    
    return alerts;
}

uint64_t PerformanceMonitor::get_current_timestamp() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

std::vector<uint64_t> PerformanceMonitor::get_latency_samples(uint64_t duration_ms) const {
    auto cutoff_time = get_current_timestamp() - (duration_ms * 1000);
    
    std::vector<uint64_t> samples;
    for (const auto& op : completed_operations_) {
        if (op.start_time >= cutoff_time && op.success) {
            samples.push_back(op.get_latency_ms());
        }
    }
    
    return samples;
}

double PerformanceMonitor::calculate_percentile(const std::vector<uint64_t>& sorted_values, double percentile) const {
    if (sorted_values.empty()) {
        return 0.0;
    }
    
    double index = percentile * (sorted_values.size() - 1);
    size_t lower = static_cast<size_t>(std::floor(index));
    size_t upper = static_cast<size_t>(std::ceil(index));
    
    if (lower == upper) {
        return sorted_values[lower];
    }
    
    double weight = index - lower;
    return sorted_values[lower] * (1.0 - weight) + sorted_values[upper] * weight;
}

void PerformanceMonitor::cleanup_old_operations() {
    auto cutoff_time = get_current_timestamp() - (3600 * 1000000); // 1 hour in microseconds
    
    auto it = std::remove_if(completed_operations_.begin(), completed_operations_.end(),
        [cutoff_time](const OperationMetrics& op) {
            return op.start_time < cutoff_time;
        });
    
    completed_operations_.erase(it, completed_operations_.end());
}

double PerformanceMonitor::measure_cpu_usage() const {
    // Simplified CPU measurement - in practice would read from /proc/stat
    return 50.0; // Placeholder
}

double PerformanceMonitor::measure_memory_usage() const {
    // Simplified memory measurement - in practice would read from /proc/meminfo
    return 512.0; // Placeholder in MB
}

double PerformanceMonitor::measure_network_usage() const {
    // Simplified network measurement - in practice would read from /proc/net/dev
    return 25.0; // Placeholder percentage
}

bool PerformanceMonitor::is_performance_degraded() const {
    PerformanceStats current = get_current_stats();
    PerformanceStats historical = get_historical_stats(300000); // 5 minutes
    
    // Check if current performance is significantly worse than historical
    return (current.average_latency_ms > historical.average_latency_ms * 1.5) ||
           (current.throughput_ops_per_sec < historical.throughput_ops_per_sec * 0.8);
}

ReplicationMode PerformanceMonitor::analyze_optimal_mode() const {
    PerformanceStats chain_stats = get_chain_stats();
    PerformanceStats quorum_stats = get_quorum_stats();
    
    // Simple heuristic: prefer the mode with lower latency and higher throughput
    if (chain_stats.average_latency_ms < quorum_stats.average_latency_ms &&
        chain_stats.throughput_ops_per_sec > quorum_stats.throughput_ops_per_sec) {
        return ReplicationMode::CHAIN_ONLY;
    } else if (quorum_stats.average_latency_ms < chain_stats.average_latency_ms &&
               quorum_stats.throughput_ops_per_sec > chain_stats.throughput_ops_per_sec) {
        return ReplicationMode::QUORUM_ONLY;
    } else {
        return ReplicationMode::HYBRID_AUTO;
    }
}

double PerformanceMonitor::calculate_efficiency_score(ReplicationMode mode) const {
    PerformanceStats stats;
    
    switch (mode) {
        case ReplicationMode::CHAIN_ONLY:
            stats = get_chain_stats();
            break;
        case ReplicationMode::QUORUM_ONLY:
            stats = get_quorum_stats();
            break;
        case ReplicationMode::HYBRID_AUTO:
            stats = get_hybrid_stats();
            break;
    }
    
    // Calculate efficiency as combination of throughput and inverse latency
    double throughput_score = std::min(stats.throughput_ops_per_sec / 1000.0, 1.0); // Normalize
    double latency_score = std::max(0.0, 1.0 - (stats.average_latency_ms / 1000.0)); // Inverse latency
    
    return (throughput_score * 0.6) + (latency_score * 0.4);
}

} // namespace replication 