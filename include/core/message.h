#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace replication {

enum class MessageType {
    READ_REQUEST,
    READ_RESPONSE,
    WRITE_REQUEST,
    WRITE_RESPONSE,
    HEARTBEAT,
    NODE_FAILURE,
    NODE_RECOVERY,
    CHAIN_UPDATE,
    QUORUM_PREPARE,
    QUORUM_PROMISE,
    QUORUM_ACCEPT,
    QUORUM_ACCEPTED,
    QUORUM_COMMIT,
    QUORUM_ABORT,
    MODE_SWITCH,
    CACHE_UPDATE,
    BATCH_REQUEST,
    BATCH_RESPONSE
};

enum class ReplicationMode {
    CHAIN_ONLY,
    QUORUM_ONLY,
    HYBRID
};

struct Message {
    MessageType type;
    uint32_t sender_id;
    uint32_t receiver_id;
    std::string key;
    std::string value;
    bool success;
    uint64_t timestamp;
    uint32_t sequence_number;
    std::string correlation_id;
    std::vector<uint32_t> target_nodes;
    std::string metadata;
    
    Message() : type(MessageType::READ_REQUEST), sender_id(0), receiver_id(0), 
                success(false), timestamp(0), sequence_number(0) {}
    
    std::string serialize() const;
    static Message deserialize(const std::string& data);
};

struct RequestMetrics {
    uint64_t start_time;
    uint64_t end_time;
    uint32_t retry_count;
    bool from_cache;
    std::string protocol_used;
    uint32_t latency_ms;
    bool success;
    
    RequestMetrics() : start_time(0), end_time(0), retry_count(0), 
                      from_cache(false), latency_ms(0), success(false) {}
};

} // namespace replication
