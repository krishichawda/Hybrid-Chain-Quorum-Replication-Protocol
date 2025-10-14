#pragma once

#include "../core/message.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>

namespace replication {

struct NodeEndpoint {
    std::string hostname;
    uint16_t port;
    bool is_active;
    uint64_t last_heartbeat;
    
    NodeEndpoint() : port(0), is_active(false), last_heartbeat(0) {}
    NodeEndpoint(const std::string& host, uint16_t p) 
        : hostname(host), port(p), is_active(true), last_heartbeat(0) {}
};

class NetworkManager {
public:
    NetworkManager(uint32_t node_id, uint16_t listen_port);
    ~NetworkManager();
    
    // Network lifecycle
    bool start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // Node management
    void add_node(uint32_t node_id, const std::string& hostname, uint16_t port);
    void remove_node(uint32_t node_id);
    bool is_node_reachable(uint32_t node_id) const;
    void update_node_status(uint32_t node_id, bool is_active);
    
    // Message handling
    bool send_message(uint32_t target_node, const Message& message);
    bool broadcast_message(const std::vector<uint32_t>& target_nodes, const Message& message);
    void set_message_handler(std::function<void(const Message&)> handler);
    
    // Performance optimizations
    void enable_compression(bool enable) { compression_enabled_ = enable; }
    void enable_message_batching(bool enable) { message_batching_enabled_ = enable; }
    void set_batch_timeout(uint64_t timeout_ms) { batch_timeout_ = timeout_ms; }
    void set_connection_pool_size(size_t size) { connection_pool_size_ = size; }
    
    // Reliability features
    void enable_reliable_delivery(bool enable) { reliable_delivery_enabled_ = enable; }
    void set_retry_attempts(int attempts) { max_retry_attempts_ = attempts; }
    void set_timeout(uint64_t timeout_ms) { message_timeout_ = timeout_ms; }
    
    // Network monitoring
    double get_network_latency(uint32_t target_node) const;
    double get_packet_loss_rate(uint32_t target_node) const;
    size_t get_message_queue_size() const;
    
    // Heartbeat management
    void start_heartbeat(uint64_t interval_ms);
    void stop_heartbeat();
    void handle_heartbeat(uint32_t sender_node);

private:
    uint32_t node_id_;
    uint16_t listen_port_;
    std::atomic<bool> running_;
    
    // Node registry
    std::unordered_map<uint32_t, NodeEndpoint> known_nodes_;
    mutable std::mutex nodes_mutex_;
    
    // Message handling
    std::function<void(const Message&)> message_handler_;
    std::thread listener_thread_;
    std::thread sender_thread_;
    
    // Performance features
    bool compression_enabled_;
    bool message_batching_enabled_;
    bool reliable_delivery_enabled_;
    uint64_t batch_timeout_;
    size_t connection_pool_size_;
    int max_retry_attempts_;
    uint64_t message_timeout_;
    
    // Message batching
    std::unordered_map<uint32_t, std::vector<Message>> pending_batches_;
    mutable std::mutex batch_mutex_;
    std::thread batch_processor_thread_;
    
    // Heartbeat system
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_;
    uint64_t heartbeat_interval_;
    
    // Connection management
    std::unordered_map<uint32_t, int> connection_pool_;
    mutable std::mutex connection_mutex_;
    
    // Performance tracking
    mutable std::mutex stats_mutex_;
    std::unordered_map<uint32_t, std::vector<uint64_t>> latency_history_;
    std::unordered_map<uint32_t, size_t> message_counts_;
    std::unordered_map<uint32_t, size_t> failed_sends_;
    
    // Internal methods
    void listener_loop();
    void sender_loop();
    void batch_processor_loop();
    void heartbeat_loop();
    
    // Network operations
    bool establish_connection(uint32_t target_node);
    void close_connection(uint32_t target_node);
    bool send_raw_message(uint32_t target_node, const std::vector<uint8_t>& data);
    bool receive_raw_message(std::vector<uint8_t>& data);
    
    // Message processing
    void process_incoming_message(const std::vector<uint8_t>& raw_data);
    void process_message_batch(uint32_t target_node);
    bool retry_failed_message(uint32_t target_node, const Message& message);
    
    // Optimization helpers
    std::vector<uint8_t> compress_data(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decompress_data(const std::vector<uint8_t>& compressed_data);
    void update_network_stats(uint32_t target_node, uint64_t latency, bool success);
};

} // namespace replication 