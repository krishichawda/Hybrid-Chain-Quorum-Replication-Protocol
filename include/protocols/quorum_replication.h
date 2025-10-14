#pragma once

#include "../core/message.h"
#include "../core/node.h"
#include <vector>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace replication {

enum class QuorumPhase {
    PREPARE,
    ACCEPT,
    COMMIT
};

struct QuorumState {
    uint64_t proposal_number;
    QuorumPhase phase;
    std::string key;
    std::string value;
    std::unordered_set<uint32_t> promised_nodes;
    std::unordered_set<uint32_t> accepted_nodes;
    uint64_t start_time;
    
    bool has_majority(size_t total_nodes) const {
        size_t required = (total_nodes / 2) + 1;
        return promised_nodes.size() >= required;
    }
    
    bool has_accept_majority(size_t total_nodes) const {
        size_t required = (total_nodes / 2) + 1;
        return accepted_nodes.size() >= required;
    }
};

class QuorumReplication {
public:
    QuorumReplication(std::shared_ptr<Node> node, const std::vector<uint32_t>& quorum_nodes);
    ~QuorumReplication() = default;
    
    // Core operations
    bool process_read(const Message& request, Message& response);
    bool process_write(const Message& request, Message& response);
    
    // Configuration management
    void update_quorum_nodes(const std::vector<uint32_t>& new_nodes);
    size_t get_quorum_size() const;
    bool is_in_quorum(uint32_t node_id) const;
    
    // Paxos message handlers
    void handle_prepare(const Message& message);
    void handle_promise(const Message& message);
    void handle_accept(const Message& message);
    void handle_accepted(const Message& message);
    
    // Fault tolerance
    void handle_node_failure(uint32_t failed_node);
    void handle_node_recovery(uint32_t recovered_node);
    
    // Optimizations
    void enable_fast_quorum(bool enable) { fast_quorum_enabled_ = enable; }
    void enable_read_optimization(bool enable) { read_optimization_enabled_ = enable; }
    void enable_adaptive_quorum(bool enable) { adaptive_quorum_enabled_ = enable; }
    void set_timeout(uint64_t timeout_ms) { operation_timeout_ = timeout_ms; }
    void adjust_quorum_size_based_on_load();
    
    // Performance metrics
    double get_consensus_success_rate() const;
    double get_average_consensus_time() const;

private:
    std::shared_ptr<Node> node_;
    std::vector<uint32_t> quorum_nodes_;
    size_t quorum_size_;
    
    std::atomic<uint64_t> next_proposal_number_;
    
    bool fast_quorum_enabled_;
    bool read_optimization_enabled_;
    bool adaptive_quorum_enabled_;
    uint64_t operation_timeout_;
    
    std::unordered_map<uint64_t, QuorumState> active_proposals_;
    std::mutex consensus_mutex_;
    
    // Performance tracking
    std::atomic<size_t> successful_consensus_;
    std::atomic<size_t> failed_consensus_;
    std::vector<uint64_t> consensus_times_;
    
    // Internal methods
    uint64_t generate_proposal_number();
    bool initiate_consensus(const std::string& key, const std::string& value);
    void cleanup_expired_proposals();
    
    bool send_prepare_messages(uint64_t proposal_number, const std::string& key);
    bool send_accept_messages(uint64_t proposal_number, const std::string& key, const std::string& value);
    
    size_t calculate_optimal_quorum_size();
    bool can_use_fast_path(const Message& request);
    std::vector<uint32_t> select_optimal_quorum_subset();
};

} // namespace replication 