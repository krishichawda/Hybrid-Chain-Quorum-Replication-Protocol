#include "network/network_manager.h"
#include "utils/logger.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>

// Simplified networking for demo purposes
// In a production system, this would use proper socket programming

namespace replication {

NetworkManager::NetworkManager(uint32_t node_id, uint16_t listen_port)
    : node_id_(node_id)
    , listen_port_(listen_port)
    , running_(false)
    , compression_enabled_(false)
    , message_batching_enabled_(true)
    , reliable_delivery_enabled_(true)
    , batch_timeout_(100)
    , connection_pool_size_(10)
    , max_retry_attempts_(3)
    , message_timeout_(5000)
    , heartbeat_running_(false)
    , heartbeat_interval_(30000) {
    
    LOG_INFO("NetworkManager initialized for node " + std::to_string(node_id_) + 
             " on port " + std::to_string(listen_port_));
}

NetworkManager::~NetworkManager() {
    stop();
}

bool NetworkManager::start() {
    if (running_.load()) {
        LOG_WARNING("NetworkManager is already running");
        return false;
    }
    
    running_.store(true);
    
    // Start listener thread (simplified)
    listener_thread_ = std::thread(&NetworkManager::listener_loop, this);
    
    // Start sender thread
    sender_thread_ = std::thread(&NetworkManager::sender_loop, this);
    
    // Start batch processor if batching is enabled
    if (message_batching_enabled_) {
        batch_processor_thread_ = std::thread(&NetworkManager::batch_processor_loop, this);
    }
    
    LOG_INFO("NetworkManager started successfully");
    return true;
}

void NetworkManager::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    // Stop heartbeat if running
    if (heartbeat_running_.load()) {
        stop_heartbeat();
    }
    
    // Wait for threads to finish
    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }
    if (sender_thread_.joinable()) {
        sender_thread_.join();
    }
    if (batch_processor_thread_.joinable()) {
        batch_processor_thread_.join();
    }
    
    LOG_INFO("NetworkManager stopped");
}

void NetworkManager::add_node(uint32_t node_id, const std::string& hostname, uint16_t port) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    known_nodes_[node_id] = NodeEndpoint(hostname, port);
    
    LOG_INFO("Added node " + std::to_string(node_id) + " at " + hostname + ":" + std::to_string(port));
}

void NetworkManager::remove_node(uint32_t node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    auto it = known_nodes_.find(node_id);
    if (it != known_nodes_.end()) {
        known_nodes_.erase(it);
        LOG_INFO("Removed node " + std::to_string(node_id));
    }
}

bool NetworkManager::is_node_reachable(uint32_t node_id) const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    auto it = known_nodes_.find(node_id);
    if (it != known_nodes_.end()) {
        return it->second.is_active;
    }
    
    return false;
}

void NetworkManager::update_node_status(uint32_t node_id, bool is_active) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    auto it = known_nodes_.find(node_id);
    if (it != known_nodes_.end()) {
        it->second.is_active = is_active;
        
        if (is_active) {
            it->second.last_heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        }
        
        LOG_DEBUG("Node " + std::to_string(node_id) + " status updated: " + 
                  (is_active ? "active" : "inactive"));
    }
}

bool NetworkManager::send_message(uint32_t target_node, const Message& message) {
    if (!running_.load()) {
        return false;
    }
    
    // Use message batching if enabled
    if (message_batching_enabled_) {
        std::lock_guard<std::mutex> lock(batch_mutex_);
        pending_batches_[target_node].push_back(message);
        
        // Send immediately if batch is full
        if (pending_batches_[target_node].size() >= 10) {
            process_message_batch(target_node);
        }
        
        return true;
    }
    
    // Simplified sending - just log for demo
    LOG_DEBUG("Sending message type " + std::to_string(static_cast<int>(message.type)) + 
              " to node " + std::to_string(target_node));
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        message_counts_[target_node]++;
    }
    
    return true;
}

bool NetworkManager::broadcast_message(const std::vector<uint32_t>& target_nodes, const Message& message) {
    bool all_successful = true;
    
    for (uint32_t node_id : target_nodes) {
        if (node_id != node_id_) {
            if (!send_message(node_id, message)) {
                all_successful = false;
            }
        }
    }
    
    return all_successful;
}

void NetworkManager::set_message_handler(std::function<void(const Message&)> handler) {
    message_handler_ = handler;
}

double NetworkManager::get_network_latency(uint32_t target_node) const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    auto it = latency_history_.find(target_node);
    if (it != latency_history_.end() && !it->second.empty()) {
        uint64_t total_latency = 0;
        for (uint64_t latency : it->second) {
            total_latency += latency;
        }
        return static_cast<double>(total_latency) / it->second.size() / 1000.0;
    }
    
    return 0.0;
}

double NetworkManager::get_packet_loss_rate(uint32_t target_node) const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    auto sent_it = message_counts_.find(target_node);
    auto failed_it = failed_sends_.find(target_node);
    
    if (sent_it != message_counts_.end() && sent_it->second > 0) {
        size_t failed_count = (failed_it != failed_sends_.end()) ? failed_it->second : 0;
        return static_cast<double>(failed_count) / sent_it->second;
    }
    
    return 0.0;
}

size_t NetworkManager::get_message_queue_size() const {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    
    size_t total_size = 0;
    for (const auto& batch : pending_batches_) {
        total_size += batch.second.size();
    }
    
    return total_size;
}

void NetworkManager::start_heartbeat(uint64_t interval_ms) {
    if (heartbeat_running_.load()) {
        return;
    }
    
    heartbeat_interval_ = interval_ms;
    heartbeat_running_.store(true);
    heartbeat_thread_ = std::thread(&NetworkManager::heartbeat_loop, this);
    
    LOG_INFO("Heartbeat started with interval " + std::to_string(interval_ms) + "ms");
}

void NetworkManager::stop_heartbeat() {
    if (!heartbeat_running_.load()) {
        return;
    }
    
    heartbeat_running_.store(false);
    
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    
    LOG_INFO("Heartbeat stopped");
}

void NetworkManager::handle_heartbeat(uint32_t sender_node) {
    update_node_status(sender_node, true);
    LOG_DEBUG("Received heartbeat from node " + std::to_string(sender_node));
}

void NetworkManager::listener_loop() {
    // Simplified listener for demo
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Simulate receiving messages occasionally
        if (message_handler_) {
            // This would process incoming network messages in a real implementation
        }
    }
}

void NetworkManager::sender_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void NetworkManager::batch_processor_loop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(batch_timeout_));
        
        std::lock_guard<std::mutex> lock(batch_mutex_);
        for (auto& batch_pair : pending_batches_) {
            if (!batch_pair.second.empty()) {
                process_message_batch(batch_pair.first);
            }
        }
    }
}

void NetworkManager::heartbeat_loop() {
    while (heartbeat_running_.load() && running_.load()) {
        Message heartbeat_msg;
        heartbeat_msg.type = MessageType::HEARTBEAT;
        heartbeat_msg.sender_id = node_id_;
        heartbeat_msg.timestamp = heartbeat_msg.get_current_timestamp();
        
        {
            std::lock_guard<std::mutex> lock(nodes_mutex_);
            for (const auto& node_pair : known_nodes_) {
                if (node_pair.first != node_id_ && node_pair.second.is_active) {
                    send_message(node_pair.first, heartbeat_msg);
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(heartbeat_interval_));
    }
}

bool NetworkManager::establish_connection(uint32_t target_node) {
    // Simplified connection establishment
    LOG_DEBUG("Establishing connection to node " + std::to_string(target_node));
    return true;
}

void NetworkManager::close_connection(uint32_t target_node) {
    LOG_DEBUG("Closing connection to node " + std::to_string(target_node));
}

bool NetworkManager::send_raw_message(uint32_t target_node, const std::vector<uint8_t>& data) {
    // Simplified raw message sending
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Simulate network transmission
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();
    
    update_network_stats(target_node, latency, true);
    
    return true;
}

bool NetworkManager::receive_raw_message(std::vector<uint8_t>& data) {
    return true;
}

void NetworkManager::process_incoming_message(const std::vector<uint8_t>& raw_data) {
    std::vector<uint8_t> data = raw_data;
    
    if (compression_enabled_) {
        data = decompress_data(data);
    }
    
    Message message;
    if (message.deserialize(data)) {
        if (message.type == MessageType::HEARTBEAT) {
            handle_heartbeat(message.sender_id);
        } else if (message_handler_) {
            message_handler_(message);
        }
    } else {
        LOG_WARNING("Failed to deserialize incoming message");
    }
}

void NetworkManager::process_message_batch(uint32_t target_node) {
    if (pending_batches_[target_node].empty()) {
        return;
    }
    
    LOG_DEBUG("Processing message batch for node " + std::to_string(target_node) + 
              " with " + std::to_string(pending_batches_[target_node].size()) + " messages");
    
    // Simulate batch processing
    for (const auto& msg : pending_batches_[target_node]) {
        LOG_DEBUG("Batch sending message type " + std::to_string(static_cast<int>(msg.type)));
    }
    
    pending_batches_[target_node].clear();
}

bool NetworkManager::retry_failed_message(uint32_t target_node, const Message& message) {
    for (int attempt = 0; attempt < max_retry_attempts_; ++attempt) {
        if (send_message(target_node, message)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100 * (1 << attempt)));
    }
    return false;
}

std::vector<uint8_t> NetworkManager::compress_data(const std::vector<uint8_t>& data) {
    // Placeholder compression
    return data;
}

std::vector<uint8_t> NetworkManager::decompress_data(const std::vector<uint8_t>& compressed_data) {
    // Placeholder decompression
    return compressed_data;
}

void NetworkManager::update_network_stats(uint32_t target_node, uint64_t latency, bool success) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    latency_history_[target_node].push_back(latency);
    
    if (latency_history_[target_node].size() > 100) {
        latency_history_[target_node].erase(latency_history_[target_node].begin());
    }
    
    message_counts_[target_node]++;
    
    if (!success) {
        failed_sends_[target_node]++;
    }
}

} // namespace replication 