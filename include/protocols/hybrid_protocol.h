#pragma once

#include "../core/message.h"
#include "../core/node.h"
#include "chain_replication.h"
#include "quorum_replication.h"
#include <memory>
#include <chrono>

namespace replication {

enum class WorkloadPattern {
    READ_HEAVY,
    WRITE_HEAVY,
    BALANCED,
    BURSTY,
    UNKNOWN
};

struct AdaptiveMetrics {
    double read_write_ratio;
    double average_latency;
    double throughput;
    double network_partition_probability;
    size_t active_nodes;
    WorkloadPattern pattern;
    
    AdaptiveMetrics() : read_write_ratio(1.0), average_latency(0.0), 
                       throughput(0.0), network_partition_probability(0.0), 
                       active_nodes(0), pattern(WorkloadPattern::UNKNOWN) {}
};

class HybridProtocol {
public:
    HybridProtocol(std::shared_ptr<Node> node, 
                   const std::vector<uint32_t>& chain_order,
                   const std::vector<uint32_t>& quorum_nodes);
    ~HybridProtocol() = default;
    
    // Main protocol interface
    bool process_read(const Message& request, Message& response);
    bool process_write(const Message& request, Message& response);
    
    // Adaptive mode switching
    void enable_adaptive_switching(bool enable) { adaptive_switching_enabled_ = enable; }
    void update_workload_metrics(const AdaptiveMetrics& metrics);
    ReplicationMode select_optimal_mode(const Message& request);
    
    // Protocol management
    void update_chain_configuration(const std::vector<uint32_t>& new_chain);
    void update_quorum_configuration(const std::vector<uint32_t>& new_quorum);
    
    // Performance optimizations
    void enable_intelligent_routing(bool enable) { intelligent_routing_enabled_ = enable; }
    void enable_load_balancing(bool enable) { load_balancing_enabled_ = enable; }
    void enable_caching(bool enable) { caching_enabled_ = enable; }
    
    // Fault tolerance enhancements
    void handle_network_partition();
    void handle_node_failure(uint32_t failed_node);
    void handle_node_recovery(uint32_t recovered_node);
    
    // Advanced features
    void enable_speculative_execution(bool enable) { speculative_execution_enabled_ = enable; }
    void enable_request_batching(bool enable) { request_batching_enabled_ = enable; }
    void set_switching_threshold(double threshold) { switching_threshold_ = threshold; }
    
    // Performance metrics
    double get_hybrid_efficiency() const;
    double get_mode_switching_overhead() const;
    AdaptiveMetrics get_current_metrics() const;
    
    // Configuration
    void set_read_preference(ReplicationMode mode) { read_preference_ = mode; }
    void set_write_preference(ReplicationMode mode) { write_preference_ = mode; }

private:
    std::shared_ptr<Node> node_;
    std::unique_ptr<ChainReplication> chain_protocol_;
    std::unique_ptr<QuorumReplication> quorum_protocol_;
    
    // Adaptive switching
    bool adaptive_switching_enabled_;
    AdaptiveMetrics current_metrics_;
    ReplicationMode current_mode_;
    ReplicationMode read_preference_;
    ReplicationMode write_preference_;
    double switching_threshold_;
    
    // Performance optimizations
    bool intelligent_routing_enabled_;
    bool load_balancing_enabled_;
    bool caching_enabled_;
    bool speculative_execution_enabled_;
    bool request_batching_enabled_;
    
    // Caching layer
    std::unordered_map<std::string, std::pair<std::string, uint64_t>> cache_;
    std::mutex cache_mutex_;
    uint64_t cache_ttl_;
    
    // Request batching
    std::vector<Message> pending_reads_;
    std::vector<Message> pending_writes_;
    std::mutex batch_mutex_;
    
    // Performance tracking
    mutable std::mutex metrics_mutex_;
    std::vector<double> mode_switching_times_;
    std::atomic<size_t> chain_operations_;
    std::atomic<size_t> quorum_operations_;
    std::atomic<size_t> cache_hits_;
    std::atomic<size_t> cache_misses_;
    
    // Decision algorithms
    ReplicationMode decide_protocol_for_read(const Message& request);
    ReplicationMode decide_protocol_for_write(const Message& request);
    bool should_switch_mode(ReplicationMode target_mode);
    
    // Optimization methods
    bool try_cache_read(const std::string& key, std::string& value);
    void update_cache(const std::string& key, const std::string& value);
    void process_batched_requests();
    
    // Speculative execution
    void start_speculative_read(const Message& request);
    void start_speculative_write(const Message& request);
    
    // Load balancing
    uint32_t select_optimal_replica_for_read();
    std::vector<uint32_t> select_optimal_nodes_for_write();
    
    // Metrics collection
    void update_performance_metrics(const Message& request, uint64_t latency, bool success);
    WorkloadPattern analyze_workload_pattern();
    double calculate_network_health();
};

} // namespace replication 