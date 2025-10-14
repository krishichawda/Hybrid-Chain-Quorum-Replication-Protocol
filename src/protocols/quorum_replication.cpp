#include "protocols/quorum_replication.h"
#include "utils/logger.h"
#include <algorithm>
#include <chrono>
#include <thread>

namespace replication {

QuorumReplication::QuorumReplication(std::shared_ptr<Node> node, const std::vector<uint32_t>& quorum_nodes)
    : node_(node)
    , quorum_nodes_(quorum_nodes)
    , quorum_size_((quorum_nodes.size() / 2) + 1)
    , next_proposal_number_(1)
    , fast_quorum_enabled_(true)
    , read_optimization_enabled_(true)
    , adaptive_quorum_enabled_(true)
    , operation_timeout_(5000) // 5 seconds
    , successful_consensus_(0)
    , failed_consensus_(0) {
    
    LOG_INFO("QuorumReplication initialized with " + std::to_string(quorum_nodes_.size()) + 
             " nodes, quorum size: " + std::to_string(quorum_size_));
}

bool QuorumReplication::process_read(const Message& request, Message& response) {
    response.type = MessageType::READ_RESPONSE;
    response.sender_id = node_->get_node_id();
    response.timestamp = request.get_current_timestamp();
    response.key = request.key;
    response.sequence_number = request.sequence_number;
    
    // Fast path for single node quorums - no consensus needed
    if (quorum_nodes_.size() == 1) {
        std::string value;
        if (node_->read(request.key, value)) {
            response.value = value;
            response.success = true;
            successful_consensus_.fetch_add(1);
            LOG_DEBUG("Single-node quorum read successful for key: " + request.key);
            return true;
        } else {
            response.success = false;
            failed_consensus_.fetch_add(1);
            LOG_DEBUG("Single-node quorum read failed for key: " + request.key);
            return false;
        }
    }
    
    // Fast path for read optimization
    if (read_optimization_enabled_ && can_use_fast_path(request)) {
        std::string value;
        if (node_->read(request.key, value)) {
            response.value = value;
            response.success = true;
            LOG_DEBUG("Fast path read successful for key: " + request.key);
            return true;
        }
    }
    
    // Consensus-based read for strong consistency
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Generate proposal number for read consensus
    uint64_t proposal_num = generate_proposal_number();
    
    // Initiate read consensus
    QuorumState read_state;
    read_state.proposal_number = proposal_num;
    read_state.phase = QuorumPhase::PREPARE;
    read_state.key = request.key;
    read_state.start_time = std::chrono::duration_cast<std::chrono::microseconds>(
        start_time.time_since_epoch()).count();
    
    {
        std::lock_guard<std::mutex> lock(consensus_mutex_);
        active_proposals_[proposal_num] = read_state;
    }
    
    // Send prepare messages
    if (send_prepare_messages(proposal_num, request.key)) {
        // Wait for consensus or timeout
        auto timeout = start_time + std::chrono::milliseconds(operation_timeout_);
        
        while (std::chrono::high_resolution_clock::now() < timeout) {
            std::lock_guard<std::mutex> lock(consensus_mutex_);
            auto it = active_proposals_.find(proposal_num);
            if (it != active_proposals_.end() && it->second.has_majority(quorum_nodes_.size())) {
                // Read consensus successful
                std::string value;
                if (node_->read(request.key, value)) {
                    response.value = value;
                    response.success = true;
                    successful_consensus_.fetch_add(1);
                    
                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                        end_time - start_time).count();
                    consensus_times_.push_back(duration);
                    
                    LOG_DEBUG("Quorum read successful for key: " + request.key);
                    active_proposals_.erase(it);
                    return true;
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Cleanup on timeout/failure
        std::lock_guard<std::mutex> lock(consensus_mutex_);
        active_proposals_.erase(proposal_num);
    }
    
    response.success = false;
    failed_consensus_.fetch_add(1);
    LOG_DEBUG("Quorum read failed for key: " + request.key);
    return false;
}

bool QuorumReplication::process_write(const Message& request, Message& response) {
    response.type = MessageType::WRITE_RESPONSE;
    response.sender_id = node_->get_node_id();
    response.timestamp = request.get_current_timestamp();
    response.key = request.key;
    response.sequence_number = request.sequence_number;
    
    // Fast path for single node quorums - no consensus needed
    if (quorum_nodes_.size() == 1) {
        bool success = node_->write(request.key, request.value);
        response.success = success;
        
        if (success) {
            successful_consensus_.fetch_add(1);
            LOG_DEBUG("Single-node quorum write successful for key: " + request.key);
        } else {
            failed_consensus_.fetch_add(1);
            LOG_ERROR("Single-node quorum write failed for key: " + request.key);
        }
        return success;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Initiate consensus for write
    bool success = initiate_consensus(request.key, request.value);
    
    if (success) {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        consensus_times_.push_back(duration);
        successful_consensus_.fetch_add(1);
        LOG_DEBUG("Quorum write successful for key: " + request.key);
    } else {
        failed_consensus_.fetch_add(1);
        LOG_ERROR("Quorum write failed for key: " + request.key);
    }
    
    response.success = success;
    return success;
}

void QuorumReplication::update_quorum_nodes(const std::vector<uint32_t>& new_nodes) {
    std::lock_guard<std::mutex> lock(consensus_mutex_);
    
    quorum_nodes_ = new_nodes;
    quorum_size_ = (new_nodes.size() / 2) + 1;
    
    LOG_INFO("Quorum nodes updated, new size: " + std::to_string(quorum_nodes_.size()) + 
             ", quorum threshold: " + std::to_string(quorum_size_));
}

size_t QuorumReplication::get_quorum_size() const {
    return quorum_size_;
}

bool QuorumReplication::is_in_quorum(uint32_t node_id) const {
    return std::find(quorum_nodes_.begin(), quorum_nodes_.end(), node_id) != quorum_nodes_.end();
}

void QuorumReplication::handle_prepare(const Message& message) {
    // Handle incoming prepare message (Phase 1 of Paxos)
    Message promise_msg;
    promise_msg.type = MessageType::QUORUM_PROMISE;
    promise_msg.sender_id = node_->get_node_id();
    promise_msg.timestamp = message.get_current_timestamp();
    promise_msg.sequence_number = message.sequence_number;
    promise_msg.success = true; // Promise to not accept lower numbered proposals
    
    node_->send_message(message.sender_id, promise_msg);
    LOG_DEBUG("Sent promise for proposal " + std::to_string(message.sequence_number));
}

void QuorumReplication::handle_promise(const Message& message) {
    // Handle incoming promise message
    std::lock_guard<std::mutex> lock(consensus_mutex_);
    
    auto it = active_proposals_.find(message.sequence_number);
    if (it != active_proposals_.end()) {
        it->second.promised_nodes.insert(message.sender_id);
        
        if (it->second.has_majority(quorum_nodes_.size())) {
            // Move to accept phase
            it->second.phase = QuorumPhase::ACCEPT;
            send_accept_messages(it->second.proposal_number, it->second.key, it->second.value);
        }
    }
}

void QuorumReplication::handle_accept(const Message& message) {
    // Handle incoming accept message (Phase 2 of Paxos)
    Message accepted_msg;
    accepted_msg.type = MessageType::QUORUM_ACCEPTED;
    accepted_msg.sender_id = node_->get_node_id();
    accepted_msg.timestamp = message.get_current_timestamp();
    accepted_msg.sequence_number = message.sequence_number;
    accepted_msg.success = true;
    
    // Apply the write locally
    node_->write(message.key, message.value);
    
    node_->send_message(message.sender_id, accepted_msg);
    LOG_DEBUG("Accepted proposal " + std::to_string(message.sequence_number));
}

void QuorumReplication::handle_accepted(const Message& message) {
    // Handle incoming accepted message
    std::lock_guard<std::mutex> lock(consensus_mutex_);
    
    auto it = active_proposals_.find(message.sequence_number);
    if (it != active_proposals_.end()) {
        it->second.accepted_nodes.insert(message.sender_id);
        
        if (it->second.has_accept_majority(quorum_nodes_.size())) {
            // Consensus achieved
            it->second.phase = QuorumPhase::COMMIT;
            LOG_DEBUG("Consensus achieved for proposal " + std::to_string(message.sequence_number));
        }
    }
}

void QuorumReplication::handle_node_failure(uint32_t failed_node) {
    std::lock_guard<std::mutex> lock(consensus_mutex_);
    
    auto it = std::find(quorum_nodes_.begin(), quorum_nodes_.end(), failed_node);
    if (it != quorum_nodes_.end()) {
        quorum_nodes_.erase(it);
        quorum_size_ = (quorum_nodes_.size() / 2) + 1;
        
        LOG_WARNING("Node " + std::to_string(failed_node) + " failed, removed from quorum");
        
        // Adjust quorum size if adaptive quorum is enabled
        if (adaptive_quorum_enabled_) {
            adjust_quorum_size_based_on_load();
        }
    }
}

void QuorumReplication::handle_node_recovery(uint32_t recovered_node) {
    std::lock_guard<std::mutex> lock(consensus_mutex_);
    
    if (!is_in_quorum(recovered_node)) {
        quorum_nodes_.push_back(recovered_node);
        quorum_size_ = (quorum_nodes_.size() / 2) + 1;
        
        LOG_INFO("Node " + std::to_string(recovered_node) + " recovered, added to quorum");
    }
}

void QuorumReplication::adjust_quorum_size_based_on_load() {
    // Dynamically adjust quorum size based on current load and network conditions
    size_t optimal_size = calculate_optimal_quorum_size();
    
    if (optimal_size != quorum_size_ && optimal_size >= 3) {
        quorum_size_ = optimal_size;
        LOG_INFO("Adaptive quorum size adjusted to: " + std::to_string(quorum_size_));
    }
}

double QuorumReplication::get_consensus_success_rate() const {
    size_t total = successful_consensus_.load() + failed_consensus_.load();
    if (total == 0) return 0.0;
    
    return static_cast<double>(successful_consensus_.load()) / total;
}

double QuorumReplication::get_average_consensus_time() const {
    if (consensus_times_.empty()) return 0.0;
    
    uint64_t total_time = 0;
    for (uint64_t time : consensus_times_) {
        total_time += time;
    }
    
    return static_cast<double>(total_time) / consensus_times_.size() / 1000.0; // Convert to ms
}

uint64_t QuorumReplication::generate_proposal_number() {
    return next_proposal_number_.fetch_add(1);
}

bool QuorumReplication::initiate_consensus(const std::string& key, const std::string& value) {
    uint64_t proposal_num = generate_proposal_number();
    
    QuorumState write_state;
    write_state.proposal_number = proposal_num;
    write_state.phase = QuorumPhase::PREPARE;
    write_state.key = key;
    write_state.value = value;
    write_state.start_time = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    {
        std::lock_guard<std::mutex> lock(consensus_mutex_);
        active_proposals_[proposal_num] = write_state;
    }
    
    // Phase 1: Prepare
    if (!send_prepare_messages(proposal_num, key)) {
        std::lock_guard<std::mutex> lock(consensus_mutex_);
        active_proposals_.erase(proposal_num);
        return false;
    }
    
    // Wait for majority promises and then move to accept phase
    auto start_time = std::chrono::high_resolution_clock::now();
    auto timeout = start_time + std::chrono::milliseconds(operation_timeout_);
    
    while (std::chrono::high_resolution_clock::now() < timeout) {
        {
            std::lock_guard<std::mutex> lock(consensus_mutex_);
            auto it = active_proposals_.find(proposal_num);
            if (it != active_proposals_.end()) {
                if (it->second.phase == QuorumPhase::COMMIT && 
                    it->second.has_accept_majority(quorum_nodes_.size())) {
                    // Consensus successful
                    node_->write(key, value);
                    active_proposals_.erase(it);
                    return true;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Timeout or failure
    {
        std::lock_guard<std::mutex> lock(consensus_mutex_);
        active_proposals_.erase(proposal_num);
    }
    
    return false;
}

void QuorumReplication::cleanup_expired_proposals() {
    std::lock_guard<std::mutex> lock(consensus_mutex_);
    
    auto current_time = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    auto it = active_proposals_.begin();
    while (it != active_proposals_.end()) {
        if (current_time - it->second.start_time > operation_timeout_ * 1000) {
            LOG_DEBUG("Cleaning up expired proposal " + std::to_string(it->first));
            it = active_proposals_.erase(it);
        } else {
            ++it;
        }
    }
}

bool QuorumReplication::send_prepare_messages(uint64_t proposal_number, const std::string& key) {
    Message prepare_msg;
    prepare_msg.type = MessageType::QUORUM_PREPARE;
    prepare_msg.sender_id = node_->get_node_id();
    prepare_msg.timestamp = prepare_msg.get_current_timestamp();
    prepare_msg.sequence_number = proposal_number;
    prepare_msg.key = key;
    
    // Send to optimal quorum subset for better performance
    std::vector<uint32_t> target_nodes;
    if (adaptive_quorum_enabled_) {
        target_nodes = select_optimal_quorum_subset();
    } else {
        target_nodes = quorum_nodes_;
    }
    
    for (uint32_t node_id : target_nodes) {
        if (node_id != node_->get_node_id()) {
            node_->send_message(node_id, prepare_msg);
        }
    }
    
    LOG_DEBUG("Sent prepare messages for proposal " + std::to_string(proposal_number));
    return true;
}

bool QuorumReplication::send_accept_messages(uint64_t proposal_number, const std::string& key, const std::string& value) {
    Message accept_msg;
    accept_msg.type = MessageType::QUORUM_ACCEPT;
    accept_msg.sender_id = node_->get_node_id();
    accept_msg.timestamp = accept_msg.get_current_timestamp();
    accept_msg.sequence_number = proposal_number;
    accept_msg.key = key;
    accept_msg.value = value;
    
    for (uint32_t node_id : quorum_nodes_) {
        if (node_id != node_->get_node_id()) {
            node_->send_message(node_id, accept_msg);
        }
    }
    
    LOG_DEBUG("Sent accept messages for proposal " + std::to_string(proposal_number));
    return true;
}

size_t QuorumReplication::calculate_optimal_quorum_size() {
    // Calculate optimal quorum size based on network conditions and load
    // This is a simplified heuristic - in practice would use more sophisticated analysis
    
    size_t base_size = (quorum_nodes_.size() / 2) + 1;
    
    // Adjust based on network health and consensus success rate
    double success_rate = get_consensus_success_rate();
    if (success_rate < 0.8) {
        // Increase quorum size for better reliability
        return std::min(base_size + 1, quorum_nodes_.size());
    } else if (success_rate > 0.95) {
        // Decrease quorum size for better performance
        return std::max(base_size - 1, size_t(3));
    }
    
    return base_size;
}

bool QuorumReplication::can_use_fast_path(const Message& request) {
    // Determine if fast path can be used for read optimization
    return request.is_read_operation() && 
           !request.key.empty() && 
           fast_quorum_enabled_;
}

std::vector<uint32_t> QuorumReplication::select_optimal_quorum_subset() {
    // Select optimal subset of nodes for quorum operations
    // This would typically consider network latency, node load, etc.
    
    if (quorum_nodes_.size() <= quorum_size_) {
        return quorum_nodes_;
    }
    
    // For now, just return the first quorum_size_ nodes
    // In practice, this would use sophisticated selection algorithms
    std::vector<uint32_t> subset;
    for (size_t i = 0; i < quorum_size_ && i < quorum_nodes_.size(); ++i) {
        subset.push_back(quorum_nodes_[i]);
    }
    
    return subset;
}

} // namespace replication 