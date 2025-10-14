#pragma once

#include "../core/message.h"
#include "../core/node.h"
#include <vector>
#include <memory>

namespace replication {

class ChainReplication {
public:
    ChainReplication(std::shared_ptr<Node> node, const std::vector<uint32_t>& chain_order);
    ~ChainReplication() = default;
    
    // Chain replication operations
    bool process_read(const Message& request, Message& response);
    bool process_write(const Message& request, Message& response);
    
    // Chain management
    void update_chain_order(const std::vector<uint32_t>& new_chain);
    bool is_head() const;
    bool is_tail() const;
    uint32_t get_successor() const;
    uint32_t get_predecessor() const;
    
    // Fault tolerance
    void handle_node_failure(uint32_t failed_node);
    void handle_node_recovery(uint32_t recovered_node);
    
    // Performance optimizations
    void enable_batching(bool enable) { batching_enabled_ = enable; }
    void set_batch_size(size_t size) { batch_size_ = size; }
    void enable_pipelining(bool enable) { pipelining_enabled_ = enable; }
    
    // Metrics
    double get_chain_utilization() const;
    size_t get_chain_length() const { return chain_order_.size(); }

private:
    std::shared_ptr<Node> node_;
    std::vector<uint32_t> chain_order_;
    uint32_t my_position_;
    
    // Performance optimizations
    bool batching_enabled_;
    size_t batch_size_;
    bool pipelining_enabled_;
    std::vector<Message> write_batch_;
    
    // Internal state
    std::unordered_map<uint64_t, Message> pending_writes_;
    mutable std::mutex chain_mutex_;
    
    // Helper methods
    void find_my_position();
    bool forward_write(const Message& message);
    bool send_ack(const Message& original_request);
    void process_write_batch();
    bool validate_chain_integrity();
    
    // Optimization methods
    void optimize_chain_ordering();
    bool should_use_fast_path(const Message& request);
};

} // namespace replication 