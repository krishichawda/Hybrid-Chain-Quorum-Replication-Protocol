#include "protocols/hybrid_protocol.h"
#include "utils/logger.h"
#include <algorithm>
#include <chrono>

namespace replication {

HybridProtocol::HybridProtocol(std::shared_ptr<Node> node, 
                               const std::vector<uint32_t>& chain_order,
                               const std::vector<uint32_t>& quorum_nodes)
    : node_(node)
    , adaptive_switching_enabled_(true)
    , current_mode_(ReplicationMode::HYBRID_AUTO)
    , read_preference_(ReplicationMode::CHAIN_ONLY)
    , write_preference_(ReplicationMode::QUORUM_ONLY)
    , switching_threshold_(0.15) // 15% performance difference threshold
    , intelligent_routing_enabled_(true)
    , load_balancing_enabled_(true)
    , caching_enabled_(true)
    , speculative_execution_enabled_(false)
    , request_batching_enabled_(true)
    , cache_ttl_(30000000) // 30 seconds in microseconds
    , chain_operations_(0)
    , quorum_operations_(0)
    , cache_hits_(0)
    , cache_misses_(0) {
    
    // Initialize sub-protocols
    chain_protocol_ = std::make_unique<ChainReplication>(node_, chain_order);
    quorum_protocol_ = std::make_unique<QuorumReplication>(node_, quorum_nodes);
    
    // Enable optimizations on sub-protocols
    chain_protocol_->enable_batching(true);
    chain_protocol_->enable_pipelining(true);
    
    quorum_protocol_->enable_fast_quorum(true);
    quorum_protocol_->enable_read_optimization(true);
    quorum_protocol_->enable_adaptive_quorum(true);
    
    LOG_INFO("HybridProtocol initialized with chain (" + std::to_string(chain_order.size()) + 
             " nodes) and quorum (" + std::to_string(quorum_nodes.size()) + " nodes)");
}

bool HybridProtocol::process_read(const Message& request, Message& response) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Try cache first if enabled
    if (caching_enabled_) {
        std::string cached_value;
        if (try_cache_read(request.key, cached_value)) {
            response.type = MessageType::READ_RESPONSE;
            response.sender_id = node_->get_node_id();
            response.timestamp = request.get_current_timestamp();
            response.key = request.key;
            response.value = cached_value;
            response.success = true;
            
            cache_hits_.fetch_add(1);
            LOG_DEBUG("Cache hit for read key: " + request.key);
            return true;
        }
        cache_misses_.fetch_add(1);
    }
    
    // Determine optimal protocol for this read
    ReplicationMode mode = adaptive_switching_enabled_ ? 
                          decide_protocol_for_read(request) : read_preference_;
    
    bool success = false;
    
    // Process with selected protocol
    if (mode == ReplicationMode::CHAIN_ONLY || 
        (mode == ReplicationMode::HYBRID_AUTO && current_metrics_.read_write_ratio > 2.0)) {
        
        success = chain_protocol_->process_read(request, response);
        chain_operations_.fetch_add(1);
        LOG_DEBUG("Processed read via Chain Replication");
        
    } else if (mode == ReplicationMode::QUORUM_ONLY ||
               mode == ReplicationMode::HYBRID_AUTO) {
        
        success = quorum_protocol_->process_read(request, response);
        quorum_operations_.fetch_add(1);
        LOG_DEBUG("Processed read via Quorum Replication");
    }
    
    // Update cache if successful and caching enabled
    if (success && caching_enabled_) {
        update_cache(request.key, response.value);
    }
    
    // Update performance metrics
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();
    update_performance_metrics(request, latency, success);
    
    // Start speculative execution for future requests if enabled
    if (speculative_execution_enabled_) {
        start_speculative_read(request);
    }
    
    return success;
}

bool HybridProtocol::process_write(const Message& request, Message& response) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Invalidate cache entry if caching is enabled
    if (caching_enabled_) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_.erase(request.key);
    }
    
    // Determine optimal protocol for this write
    ReplicationMode mode = adaptive_switching_enabled_ ? 
                          decide_protocol_for_write(request) : write_preference_;
    
    bool success = false;
    
    // Process with selected protocol
    if (mode == ReplicationMode::CHAIN_ONLY || 
        (mode == ReplicationMode::HYBRID_AUTO && current_metrics_.network_partition_probability > 0.3)) {
        
        success = chain_protocol_->process_write(request, response);
        chain_operations_.fetch_add(1);
        LOG_DEBUG("Processed write via Chain Replication");
        
    } else if (mode == ReplicationMode::QUORUM_ONLY ||
               mode == ReplicationMode::HYBRID_AUTO) {
        
        success = quorum_protocol_->process_write(request, response);
        quorum_operations_.fetch_add(1);
        LOG_DEBUG("Processed write via Quorum Replication");
    }
    
    // Update performance metrics
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();
    update_performance_metrics(request, latency, success);
    
    // Start speculative execution for future requests if enabled
    if (speculative_execution_enabled_) {
        start_speculative_write(request);
    }
    
    return success;
}

void HybridProtocol::update_workload_metrics(const AdaptiveMetrics& metrics) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    current_metrics_ = metrics;
    
    // Analyze workload pattern
    current_metrics_.pattern = analyze_workload_pattern();
    
    // Check if we should switch modes
    if (adaptive_switching_enabled_) {
        ReplicationMode optimal_mode = select_optimal_mode(Message()); // Dummy message for analysis
        if (should_switch_mode(optimal_mode)) {
            auto switch_start = std::chrono::high_resolution_clock::now();
            current_mode_ = optimal_mode;
            auto switch_end = std::chrono::high_resolution_clock::now();
            
            auto switch_time = std::chrono::duration_cast<std::chrono::microseconds>(
                switch_end - switch_start).count();
            mode_switching_times_.push_back(switch_time / 1000.0); // Convert to ms
            
            LOG_INFO("Switched to mode: " + std::to_string(static_cast<int>(optimal_mode)));
        }
    }
}

ReplicationMode HybridProtocol::select_optimal_mode(const Message& /*request*/) {
    // Advanced decision algorithm for optimal mode selection
    
    double chain_score = 0.0;
    double quorum_score = 0.0;
    
    // Factor 1: Read/Write ratio
    if (current_metrics_.read_write_ratio > 3.0) {
        chain_score += 0.3; // Chain is better for read-heavy workloads
    } else if (current_metrics_.read_write_ratio < 0.5) {
        quorum_score += 0.3; // Quorum is better for write-heavy workloads
    }
    
    // Factor 2: Network partition probability
    if (current_metrics_.network_partition_probability > 0.2) {
        chain_score += 0.25; // Chain is more partition-tolerant
    } else {
        quorum_score += 0.15;
    }
    
    // Factor 3: Current latency
    if (current_metrics_.average_latency > 100.0) { // > 100ms
        // Prefer the protocol with better historical performance
        double chain_efficiency = get_hybrid_efficiency();
        if (chain_efficiency > 0.8) {
            chain_score += 0.2;
        } else {
            quorum_score += 0.2;
        }
    }
    
    // Factor 4: Number of active nodes
    if (current_metrics_.active_nodes < 5) {
        chain_score += 0.15; // Chain works better with fewer nodes
    } else {
        quorum_score += 0.1;
    }
    
    // Factor 5: Workload pattern
    switch (current_metrics_.pattern) {
        case WorkloadPattern::READ_HEAVY:
            chain_score += 0.2;
            break;
        case WorkloadPattern::WRITE_HEAVY:
            quorum_score += 0.2;
            break;
        case WorkloadPattern::BURSTY:
            chain_score += 0.1; // Better batching in chain
            break;
        default:
            break;
    }
    
    // Return the protocol with higher score
    if (chain_score > quorum_score + switching_threshold_) {
        return ReplicationMode::CHAIN_ONLY;
    } else if (quorum_score > chain_score + switching_threshold_) {
        return ReplicationMode::QUORUM_ONLY;
    } else {
        return ReplicationMode::HYBRID_AUTO; // Use both based on request type
    }
}

void HybridProtocol::update_chain_configuration(const std::vector<uint32_t>& new_chain) {
    chain_protocol_->update_chain_order(new_chain);
    LOG_INFO("Chain configuration updated");
}

void HybridProtocol::update_quorum_configuration(const std::vector<uint32_t>& new_quorum) {
    quorum_protocol_->update_quorum_nodes(new_quorum);
    LOG_INFO("Quorum configuration updated");
}

void HybridProtocol::handle_network_partition() {
    // Switch to chain replication during network partitions
    if (adaptive_switching_enabled_) {
        current_mode_ = ReplicationMode::CHAIN_ONLY;
        LOG_WARNING("Network partition detected, switching to Chain Replication");
    }
}

void HybridProtocol::handle_node_failure(uint32_t failed_node) {
    chain_protocol_->handle_node_failure(failed_node);
    quorum_protocol_->handle_node_failure(failed_node);
    
    // Update metrics
    current_metrics_.active_nodes = std::max(size_t(1), current_metrics_.active_nodes - 1);
    
    LOG_WARNING("Node " + std::to_string(failed_node) + " failed, protocols updated");
}

void HybridProtocol::handle_node_recovery(uint32_t recovered_node) {
    chain_protocol_->handle_node_recovery(recovered_node);
    quorum_protocol_->handle_node_recovery(recovered_node);
    
    // Update metrics
    current_metrics_.active_nodes++;
    
    LOG_INFO("Node " + std::to_string(recovered_node) + " recovered, protocols updated");
}

double HybridProtocol::get_hybrid_efficiency() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    size_t total_ops = chain_operations_.load() + quorum_operations_.load();
    if (total_ops == 0) return 0.0;
    
    // Calculate efficiency based on successful operations and switching overhead
    double cache_hit_rate = static_cast<double>(cache_hits_.load()) / 
                           (cache_hits_.load() + cache_misses_.load());
    
    // Base efficiency from protocol balance
    double protocol_balance = std::min(chain_operations_.load(), quorum_operations_.load()) / 
                             static_cast<double>(total_ops);
    
    return (cache_hit_rate * 0.4) + (protocol_balance * 0.6);
}

double HybridProtocol::get_mode_switching_overhead() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    if (mode_switching_times_.empty()) return 0.0;
    
    double total_overhead = 0.0;
    for (double time : mode_switching_times_) {
        total_overhead += time;
    }
    
    return total_overhead / mode_switching_times_.size();
}

AdaptiveMetrics HybridProtocol::get_current_metrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return current_metrics_;
}

ReplicationMode HybridProtocol::decide_protocol_for_read(const Message& /*request*/) {
    // Intelligent routing for read requests
    
    if (intelligent_routing_enabled_) {
        // Use cache hit probability and network conditions
        if (current_metrics_.network_partition_probability > 0.2) {
            return ReplicationMode::CHAIN_ONLY;
        }
        
        // For hot keys, prefer chain replication (tail reads)
        if (current_metrics_.pattern == WorkloadPattern::READ_HEAVY) {
            return ReplicationMode::CHAIN_ONLY;
        }
    }
    
    return read_preference_;
}

ReplicationMode HybridProtocol::decide_protocol_for_write(const Message& /*request*/) {
    // Intelligent routing for write requests
    
    if (intelligent_routing_enabled_) {
        // For high-consistency requirements, prefer quorum
        if (current_metrics_.pattern == WorkloadPattern::WRITE_HEAVY) {
            return ReplicationMode::QUORUM_ONLY;
        }
        
        // For sequential writes, prefer chain
        if (current_metrics_.pattern == WorkloadPattern::BURSTY) {
            return ReplicationMode::CHAIN_ONLY;
        }
    }
    
    return write_preference_;
}

bool HybridProtocol::should_switch_mode(ReplicationMode target_mode) {
    return target_mode != current_mode_;
}

bool HybridProtocol::try_cache_read(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        auto current_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        if (current_time - it->second.second < cache_ttl_) {
            value = it->second.first;
            return true;
        } else {
            // Entry expired
            cache_.erase(it);
        }
    }
    
    return false;
}

void HybridProtocol::update_cache(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto current_time = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    cache_[key] = std::make_pair(value, current_time);
    
    // Cleanup old entries if cache gets too large
    if (cache_.size() > 1000) {
        auto oldest = cache_.begin();
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.second < oldest->second.second) {
                oldest = it;
            }
        }
        cache_.erase(oldest);
    }
}

void HybridProtocol::process_batched_requests() {
    // Process batched read and write requests
    if (!request_batching_enabled_) return;
    
    std::lock_guard<std::mutex> lock(batch_mutex_);
    
    // Process read batch
    if (!pending_reads_.empty()) {
        LOG_DEBUG("Processing read batch of size " + std::to_string(pending_reads_.size()));
        // Implementation would batch process reads
        pending_reads_.clear();
    }
    
    // Process write batch
    if (!pending_writes_.empty()) {
        LOG_DEBUG("Processing write batch of size " + std::to_string(pending_writes_.size()));
        // Implementation would batch process writes
        pending_writes_.clear();
    }
}

void HybridProtocol::start_speculative_read(const Message& request) {
    // Start speculative read operations for performance
    LOG_DEBUG("Starting speculative read for key: " + request.key);
    // Implementation would predict and prefetch related data
}

void HybridProtocol::start_speculative_write(const Message& request) {
    // Start speculative write operations
    LOG_DEBUG("Starting speculative write for key: " + request.key);
    // Implementation would predict and prepare for related writes
}

uint32_t HybridProtocol::select_optimal_replica_for_read() {
    // Load balancing: select optimal replica for read operations
    if (!load_balancing_enabled_) {
        return 0; // Default to local node
    }
    
    // Would implement sophisticated load balancing algorithm
    return 0;
}

std::vector<uint32_t> HybridProtocol::select_optimal_nodes_for_write() {
    // Load balancing: select optimal nodes for write operations
    std::vector<uint32_t> optimal_nodes;
    
    if (!load_balancing_enabled_) {
        return optimal_nodes;
    }
    
    // Would implement sophisticated node selection algorithm
    return optimal_nodes;
}

void HybridProtocol::update_performance_metrics(const Message& request, uint64_t latency, bool /*success*/) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Update running averages
    current_metrics_.average_latency = (current_metrics_.average_latency * 0.9) + (latency / 1000.0 * 0.1);
    
    // Update throughput calculation would go here
    
    // Update read/write ratio
    static size_t read_count = 0;
    static size_t write_count = 0;
    
    if (request.is_read_operation()) {
        read_count++;
    } else if (request.is_write_operation()) {
        write_count++;
    }
    
    if (write_count > 0) {
        current_metrics_.read_write_ratio = static_cast<double>(read_count) / write_count;
    }
}

WorkloadPattern HybridProtocol::analyze_workload_pattern() {
    // Analyze current workload to determine pattern
    
    if (current_metrics_.read_write_ratio > 3.0) {
        return WorkloadPattern::READ_HEAVY;
    } else if (current_metrics_.read_write_ratio < 0.5) {
        return WorkloadPattern::WRITE_HEAVY;
    } else if (current_metrics_.throughput > current_metrics_.average_latency * 10) {
        return WorkloadPattern::BURSTY;
    } else {
        return WorkloadPattern::BALANCED;
    }
}

double HybridProtocol::calculate_network_health() {
    // Calculate network health score based on various metrics
    double health_score = 1.0;
    
    // Factor in partition probability
    health_score -= current_metrics_.network_partition_probability;
    
    // Factor in latency
    if (current_metrics_.average_latency > 50.0) {
        health_score -= 0.2;
    }
    
    return std::max(0.0, health_score);
}

} // namespace replication 