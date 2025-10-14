#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <thread>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <cstdint>

namespace replication {

class NetworkManager;
class ChainReplication;
class QuorumReplication;
class HybridProtocol;

class Node {
public:
    Node(uint32_t node_id, const std::vector<uint32_t>& cluster_nodes);
    ~Node();
    
    // Lifecycle
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    // Data operations
    bool read(const std::string& key, std::string& value);
    bool write(const std::string& key, const std::string& value);
    bool delete_key(const std::string& key);
    
    // Cluster management
    uint32_t get_node_id() const { return node_id_; }
    uint32_t get_leader_id() const { return leader_id_; }
    const std::vector<uint32_t>& get_cluster_nodes() const { return cluster_nodes_; }
    bool is_leader() const { return node_id_ == leader_id_; }
    
    // Message handling
    void handle_message(const std::string& message_data);
    void send_message(uint32_t target_node, const std::string& message_data);
    
    // Failure handling
    void handle_node_failure(uint32_t failed_node);
    void handle_node_recovery(uint32_t recovered_node);
    
    // Performance metrics
    uint64_t get_operation_count() const { return operation_count_; }
    uint64_t get_success_count() const { return success_count_; }
    double get_success_rate() const;
    
    // Protocol access
    std::shared_ptr<ChainReplication> get_chain_protocol() { return chain_protocol_; }
    std::shared_ptr<QuorumReplication> get_quorum_protocol() { return quorum_protocol_; }
    std::shared_ptr<HybridProtocol> get_hybrid_protocol() { return hybrid_protocol_; }

private:
    uint32_t node_id_;
    uint32_t leader_id_;
    std::vector<uint32_t> cluster_nodes_;
    std::atomic<bool> running_;
    
    // Data storage
    std::unordered_map<std::string, std::string> data_store_;
    mutable std::mutex data_mutex_;
    
    // Message handling
    std::queue<std::string> message_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread message_thread_;
    
    // Performance metrics
    std::atomic<uint64_t> operation_count_;
    std::atomic<uint64_t> success_count_;
    
    // Protocol components
    std::shared_ptr<NetworkManager> network_manager_;
    std::shared_ptr<ChainReplication> chain_protocol_;
    std::shared_ptr<QuorumReplication> quorum_protocol_;
    std::shared_ptr<HybridProtocol> hybrid_protocol_;
    
    // Internal methods
    void message_processing_loop();
    void process_incoming_message(const std::string& message_data);
};

} // namespace replication
