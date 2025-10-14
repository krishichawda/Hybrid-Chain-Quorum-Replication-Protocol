#include "core/message.h"
#include <sstream>
#include <chrono>

namespace replication {

std::string Message::serialize() const {
    std::ostringstream oss;
    oss << static_cast<int>(type) << "|"
        << sender_id << "|"
        << receiver_id << "|"
        << key << "|"
        << value << "|"
        << (success ? "1" : "0") << "|"
        << timestamp << "|"
        << sequence_number << "|"
        << correlation_id << "|";
    
    // Serialize target_nodes
    for (size_t i = 0; i < target_nodes.size(); ++i) {
        if (i > 0) oss << ",";
        oss << target_nodes[i];
    }
    oss << "|";
    
    oss << metadata;
    return oss.str();
}

Message Message::deserialize(const std::string& data) {
    Message msg;
    std::istringstream iss(data);
    std::string token;
    
    // Parse type
    if (std::getline(iss, token, '|')) {
        msg.type = static_cast<MessageType>(std::stoi(token));
    }
    
    // Parse sender_id
    if (std::getline(iss, token, '|')) {
        msg.sender_id = std::stoul(token);
    }
    
    // Parse receiver_id
    if (std::getline(iss, token, '|')) {
        msg.receiver_id = std::stoul(token);
    }
    
    // Parse key
    if (std::getline(iss, token, '|')) {
        msg.key = token;
    }
    
    // Parse value
    if (std::getline(iss, token, '|')) {
        msg.value = token;
    }
    
    // Parse success
    if (std::getline(iss, token, '|')) {
        msg.success = (token == "1");
    }
    
    // Parse timestamp
    if (std::getline(iss, token, '|')) {
        msg.timestamp = std::stoull(token);
    }
    
    // Parse sequence_number
    if (std::getline(iss, token, '|')) {
        msg.sequence_number = std::stoul(token);
    }
    
    // Parse correlation_id
    if (std::getline(iss, token, '|')) {
        msg.correlation_id = token;
    }
    
    // Parse target_nodes
    if (std::getline(iss, token, '|')) {
        if (!token.empty()) {
            std::istringstream node_stream(token);
            std::string node_token;
            while (std::getline(node_stream, node_token, ',')) {
                msg.target_nodes.push_back(std::stoul(node_token));
            }
        }
    }
    
    // Parse metadata
    if (std::getline(iss, token, '|')) {
        msg.metadata = token;
    }
    
    return msg;
}

} // namespace replication
