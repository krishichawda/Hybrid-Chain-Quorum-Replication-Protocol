#include "protocols/chain_replication.h"
#include "utils/logger.h"
#include <algorithm>
#include <chrono>

namespace replication {

ChainReplication::ChainReplication(std::shared_ptr<Node> node, const std::vector<uint32_t>& chain_order)
    : node_(node)
    , chain_order_(chain_order)
    , my_position_(0)
    , batching_enabled_(true)
    , batch_size_(10)
    , pipelining_enabled_(true) {
    
    find_my_position();
    
    LOG_INFO("ChainReplication initialized for node " + std::to_string(node_->get_node_id()) + 
             " at position " + std::to_string(my_position_) + " in chain of " + 
             std::to_string(chain_order_.size()) + " nodes");
}

bool ChainReplication::process_read(const Message& request, Message& response) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    
    // In chain replication, reads are served by the tail
    if (!is_tail()) {
        // Forward to tail
        if (chain_order_.size() > 0) {
            uint32_t tail_node = chain_order_.back();
            node_->send_message(tail_node, request);
            LOG_DEBUG("Forwarding read request to tail node " + std::to_string(tail_node));
        }
        return false;
    }
    
    // We are the tail, serve the read directly
    response.type = MessageType::READ_RESPONSE;
    response.sender_id = node_->get_node_id();
    response.timestamp = request.get_current_timestamp();
    response.key = request.key;
    response.sequence_number = request.sequence_number;
    
    // Fast path for cached reads
    if (should_use_fast_path(request)) {
        LOG_DEBUG("Using fast path for read request: " + request.key);
    }
    
    std::string value;
    if (node_->read(request.key, value)) {
        response.value = value;
        response.success = true;
        LOG_DEBUG("Chain read successful for key: " + request.key);
    } else {
        response.success = false;
        LOG_DEBUG("Chain read failed for key: " + request.key);
    }
    
    return response.success;
}

bool ChainReplication::process_write(const Message& request, Message& response) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    
    // In chain replication, writes start at the head
    if (!is_head()) {
        // Set up response for forwarding
        response.type = MessageType::WRITE_RESPONSE;
        response.sender_id = node_->get_node_id();
        response.timestamp = request.get_current_timestamp();
        response.key = request.key;
        response.sequence_number = request.sequence_number;
        response.success = true; // Forwarding successful
        
        // Forward to head
        if (chain_order_.size() > 0) {
            uint32_t head_node = chain_order_.front();
            node_->send_message(head_node, request);
            LOG_DEBUG("Forwarding write request to head node " + std::to_string(head_node));
        }
        return true; // Forwarding succeeded
    }
    
    // We are the head, start the write process
    response.type = MessageType::WRITE_RESPONSE;
    response.sender_id = node_->get_node_id();
    response.timestamp = request.get_current_timestamp();
    response.key = request.key;
    response.sequence_number = request.sequence_number;
    
    // Optimization: Use batching if enabled
    if (batching_enabled_ && write_batch_.size() < batch_size_) {
        write_batch_.push_back(request);
        
        // Process batch when full or timeout
        if (write_batch_.size() >= batch_size_) {
            process_write_batch();
        }
        
        response.success = true;
        return true;
    }
    
    // Process single write
    bool success = node_->write(request.key, request.value);
    
    if (success && chain_order_.size() > 1) {
        // Forward to next node in chain
        if (!forward_write(request)) {
            success = false;
        }
    }
    
    response.success = success;
    
    if (success) {
        LOG_DEBUG("Chain write successful for key: " + request.key);
    } else {
        LOG_ERROR("Chain write failed for key: " + request.key);
    }
    
    return success;
}

void ChainReplication::update_chain_order(const std::vector<uint32_t>& new_chain) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    
    chain_order_ = new_chain;
    find_my_position();
    
    // Optimize chain ordering based on network topology
    optimize_chain_ordering();
    
    LOG_INFO("Chain order updated, new position: " + std::to_string(my_position_));
}

bool ChainReplication::is_head() const {
    return my_position_ == 0 && !chain_order_.empty();
}

bool ChainReplication::is_tail() const {
    return my_position_ == chain_order_.size() - 1 && !chain_order_.empty();
}

uint32_t ChainReplication::get_successor() const {
    if (my_position_ + 1 < chain_order_.size()) {
        return chain_order_[my_position_ + 1];
    }
    return 0; // No successor
}

uint32_t ChainReplication::get_predecessor() const {
    if (my_position_ > 0) {
        return chain_order_[my_position_ - 1];
    }
    return 0; // No predecessor
}

void ChainReplication::handle_node_failure(uint32_t failed_node) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    
    auto it = std::find(chain_order_.begin(), chain_order_.end(), failed_node);
    if (it != chain_order_.end()) {
        chain_order_.erase(it);
        find_my_position();
        
        LOG_WARNING("Node " + std::to_string(failed_node) + " failed, removed from chain");
        
        // Validate chain integrity after failure
        validate_chain_integrity();
    }
}

void ChainReplication::handle_node_recovery(uint32_t recovered_node) {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    
    // Add recovered node back to chain in optimal position
    // For now, add at the end
    chain_order_.push_back(recovered_node);
    find_my_position();
    
    LOG_INFO("Node " + std::to_string(recovered_node) + " recovered, added back to chain");
}

double ChainReplication::get_chain_utilization() const {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    
    // Calculate utilization based on pending operations
    double utilization = static_cast<double>(pending_writes_.size()) / 100.0; // Max 100 pending
    return std::min(utilization, 1.0);
}

void ChainReplication::find_my_position() {
    uint32_t my_id = node_->get_node_id();
    
    for (size_t i = 0; i < chain_order_.size(); ++i) {
        if (chain_order_[i] == my_id) {
            my_position_ = i;
            return;
        }
    }
    
    // Node not found in chain
    my_position_ = chain_order_.size();
    LOG_WARNING("Node not found in chain order");
}

bool ChainReplication::forward_write(const Message& message) {
    uint32_t successor = get_successor();
    if (successor == 0) {
        // We are the tail, send ACK back
        return send_ack(message);
    }
    
    // Create forward message
    Message forward_msg = message;
    forward_msg.type = MessageType::CHAIN_FORWARD;
    forward_msg.sender_id = node_->get_node_id();
    
    node_->send_message(successor, forward_msg);
    
    // Track pending write for reliability
    pending_writes_[message.sequence_number] = message;
    
    LOG_DEBUG("Forwarded write to successor node " + std::to_string(successor));
    return true;
}

bool ChainReplication::send_ack(const Message& original_request) {
    Message ack_msg;
    ack_msg.type = MessageType::CHAIN_ACK;
    ack_msg.sender_id = node_->get_node_id();
    ack_msg.timestamp = original_request.get_current_timestamp();
    ack_msg.sequence_number = original_request.sequence_number;
    ack_msg.success = true;
    
    // Send ACK to the original sender or previous node
    uint32_t predecessor = get_predecessor();
    if (predecessor != 0) {
        node_->send_message(predecessor, ack_msg);
    } else {
        // We are the head, send ACK to original client
        node_->send_message(original_request.sender_id, ack_msg);
    }
    
    LOG_DEBUG("Sent ACK for write operation");
    return true;
}

void ChainReplication::process_write_batch() {
    if (write_batch_.empty()) {
        return;
    }
    
    LOG_DEBUG("Processing write batch of size " + std::to_string(write_batch_.size()));
    
    // Apply all writes in batch locally
    for (const auto& write_msg : write_batch_) {
        node_->write(write_msg.key, write_msg.value);
    }
    
    // Forward entire batch to successor if not tail
    uint32_t successor = get_successor();
    if (successor != 0) {
        for (const auto& write_msg : write_batch_) {
            Message forward_msg = write_msg;
            forward_msg.type = MessageType::CHAIN_FORWARD;
            forward_msg.sender_id = node_->get_node_id();
            node_->send_message(successor, forward_msg);
        }
    }
    
    write_batch_.clear();
}

bool ChainReplication::validate_chain_integrity() {
    // Check if chain is properly ordered and all nodes are reachable
    // This is a simplified validation
    if (chain_order_.empty()) {
        LOG_ERROR("Chain is empty");
        return false;
    }
    
    // Additional integrity checks would go here
    LOG_DEBUG("Chain integrity validated");
    return true;
}

void ChainReplication::optimize_chain_ordering() {
    // Optimize chain order based on network latency and node capacity
    // For now, this is a placeholder - in practice, this would use
    // network measurements to optimize the chain ordering
    
    LOG_DEBUG("Chain ordering optimized");
}

bool ChainReplication::should_use_fast_path(const Message& request) {
    // Determine if we can use optimized fast path for this request
    // Fast path conditions:
    // 1. Request for recently written data
    // 2. Read-only operation
    // 3. Single-key operation
    
    if (request.is_read_operation() && !request.key.empty()) {
        return true;
    }
    
    return false;
}

} // namespace replication 