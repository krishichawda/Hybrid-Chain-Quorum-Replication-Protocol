#include "core/node.h"
#include "network/network_manager.h"
#include "protocols/chain_replication.h"
#include "protocols/quorum_replication.h"
#include "protocols/hybrid_protocol.h"
#include "utils/logger.h"
#include <algorithm>
#include <random>

namespace replication {

Node::Node(uint32_t node_id, const std::vector<uint32_t>& cluster_nodes)
    : node_id_(node_id), leader_id_(0), cluster_nodes_(cluster_nodes), 
      running_(false), operation_count_(0), success_count_(0) {
    
    // Initialize leader (first node in cluster)
    if (!cluster_nodes.empty()) {
        leader_id_ = cluster_nodes[0];
    }
    
    // Initialize network manager
    network_manager_ = std::make_shared<NetworkManager>(node_id_);
    
    // Initialize protocols
    chain_protocol_ = std::make_shared<ChainReplication>(this, cluster_nodes_);
    quorum_protocol_ = std::make_shared<QuorumReplication>(this, cluster_nodes_);
    hybrid_protocol_ = std::make_shared<HybridProtocol>(this, cluster_nodes_, cluster_nodes_);
}

Node::~Node() {
    stop();
}

bool Node::start() {
    if (running_) {
        return true;
    }
    
    running_ = true;
    
    // Start network manager
    if (!network_manager_->start()) {
        running_ = false;
        return false;
    }
    
    // Start message processing thread
    message_thread_ = std::thread(&Node::message_processing_loop, this);
    
    Logger::getInstance().logInfo("Node " + std::to_string(node_id_) + " started successfully");
    return true;
}

void Node::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // Notify message thread to stop
    queue_cv_.notify_all();
    
    // Wait for message thread to finish
    if (message_thread_.joinable()) {
        message_thread_.join();
    }
    
    // Stop network manager
    network_manager_->stop();
    
    Logger::getInstance().logInfo("Node " + std::to_string(node_id_) + " stopped");
}

bool Node::read(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto it = data_store_.find(key);
    if (it != data_store_.end()) {
        value = it->second;
        operation_count_++;
        success_count_++;
        return true;
    }
    operation_count_++;
    return false;
}

bool Node::write(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    data_store_[key] = value;
    operation_count_++;
    success_count_++;
    return true;
}

bool Node::delete_key(const std::string& key) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto it = data_store_.find(key);
    if (it != data_store_.end()) {
        data_store_.erase(it);
        operation_count_++;
        success_count_++;
        return true;
    }
    operation_count_++;
    return false;
}

void Node::handle_message(const std::string& message_data) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    message_queue_.push(message_data);
    queue_cv_.notify_one();
}

void Node::send_message(uint32_t target_node, const std::string& message_data) {
    network_manager_->send_message(target_node, message_data);
}

void Node::handle_node_failure(uint32_t failed_node) {
    // Remove failed node from cluster
    auto it = std::find(cluster_nodes_.begin(), cluster_nodes_.end(), failed_node);
    if (it != cluster_nodes_.end()) {
        cluster_nodes_.erase(it);
    }
    
    // Update leader if necessary
    if (failed_node == leader_id_ && !cluster_nodes_.empty()) {
        leader_id_ = cluster_nodes_[0];
    }
    
    Logger::getInstance().logWarning("Node " + std::to_string(failed_node) + " failed, removed from cluster");
}

void Node::handle_node_recovery(uint32_t recovered_node) {
    // Add recovered node back to cluster
    if (std::find(cluster_nodes_.begin(), cluster_nodes_.end(), recovered_node) == cluster_nodes_.end()) {
        cluster_nodes_.push_back(recovered_node);
        std::sort(cluster_nodes_.begin(), cluster_nodes_.end());
    }
    
    Logger::getInstance().logInfo("Node " + std::to_string(recovered_node) + " recovered, added back to cluster");
}

double Node::get_success_rate() const {
    uint64_t ops = operation_count_.load();
    if (ops == 0) return 0.0;
    return static_cast<double>(success_count_.load()) / ops;
}

void Node::message_processing_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] { return !message_queue_.empty() || !running_; });
        
        if (!running_) break;
        
        if (!message_queue_.empty()) {
            std::string message_data = message_queue_.front();
            message_queue_.pop();
            lock.unlock();
            
            process_incoming_message(message_data);
        }
    }
}

void Node::process_incoming_message(const std::string& message_data) {
    try {
        Message msg = Message::deserialize(message_data);
        
        // Route message to appropriate protocol
        switch (msg.type) {
            case MessageType::READ_REQUEST:
            case MessageType::WRITE_REQUEST:
                // Let hybrid protocol handle routing
                break;
            default:
                // Handle other message types
                break;
        }
    } catch (const std::exception& e) {
        Logger::getInstance().logError("Failed to process message: " + std::string(e.what()));
    }
}

} // namespace replication
