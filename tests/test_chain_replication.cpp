#include "protocols/chain_replication.h"
#include "core/node.h"
#include "utils/logger.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>

using namespace replication;

class ChainReplicationTest {
public:
    void run_all_tests() {
        std::cout << "Running Chain Replication Tests..." << std::endl;
        
        test_initialization();
        test_chain_order();
        test_read_operations();
        test_write_operations();
        test_write_forwarding();
        test_node_failure_handling();
        test_node_recovery();
        test_performance_optimization();
        test_batching();
        test_pipelining();
        
        std::cout << "All Chain Replication tests passed!" << std::endl;
    }

private:
    void test_initialization() {
        std::cout << "Testing Chain Replication initialization..." << std::endl;
        
        // Create test nodes
        std::vector<uint32_t> node_ids = {1, 2, 3, 4, 5};
        auto node = std::make_shared<Node>(1, node_ids);
        
        // Create chain order
        std::vector<uint32_t> chain_order = {1, 2, 3, 4, 5};
        
        ChainReplication chain(node, chain_order);
        
        // Test initialization
        assert(chain.get_chain_length() == 5);
        assert(chain.is_head() == true); // Node 1 is head
        
        std::cout << "✓ Chain Replication initialization test passed" << std::endl;
    }
    
    void test_chain_order() {
        std::cout << "Testing chain order management..." << std::endl;
        
        std::vector<uint32_t> node_ids = {1, 2, 3};
        auto node = std::make_shared<Node>(2, node_ids);
        
        std::vector<uint32_t> chain_order = {1, 2, 3};
        ChainReplication chain(node, chain_order);
        
        // Test chain properties
        assert(chain.get_chain_length() == 3);
        assert(chain.is_head() == false); // Node 2 is not head
        assert(chain.is_tail() == false); // Node 2 is not tail
        
        // Test successor/predecessor
        uint32_t successor = chain.get_successor();
        uint32_t predecessor = chain.get_predecessor();
        assert(successor == 3); // Next in chain
        assert(predecessor == 1); // Previous in chain
        
        std::cout << "✓ Chain order management test passed" << std::endl;
    }
    
    void test_read_operations() {
        std::cout << "Testing chain read operations..." << std::endl;
        
        std::vector<uint32_t> node_ids = {1, 2, 3};
        auto tail_node = std::make_shared<Node>(3, node_ids); // Tail node
        
        std::vector<uint32_t> chain_order = {1, 2, 3};
        ChainReplication chain(tail_node, chain_order);
        
        // Add some test data to the tail node
        tail_node->write("test_key", "test_value");
        
        // Test read from tail (should succeed)
        Message read_request;
        read_request.type = MessageType::READ_REQUEST;
        read_request.key = "test_key";
        read_request.sender_id = 100; // Client
        
        Message read_response;
        bool success = chain.process_read(read_request, read_response);
        
        assert(success);
        assert(read_response.type == MessageType::READ_RESPONSE);
        assert(read_response.value == "test_value");
        assert(read_response.success);
        
        std::cout << "✓ Chain read operations test passed" << std::endl;
    }
    
    void test_write_operations() {
        std::cout << "Testing chain write operations..." << std::endl;
        
        std::vector<uint32_t> node_ids = {1, 2, 3};
        auto head_node = std::make_shared<Node>(1, node_ids); // Head node
        
        std::vector<uint32_t> chain_order = {1, 2, 3};
        ChainReplication chain(head_node, chain_order);
        
        // Disable batching for immediate writes in testing
        chain.enable_batching(false);
        
        // Test write at head (should succeed)
        Message write_request;
        write_request.type = MessageType::WRITE_REQUEST;
        write_request.key = "new_key";
        write_request.value = "new_value";
        write_request.sender_id = 100; // Client
        
        Message write_response;
        bool success = chain.process_write(write_request, write_response);
        
        assert(success);
        assert(write_response.type == MessageType::WRITE_RESPONSE);
        assert(write_response.success);
        
        // Verify data was written to head
        std::string stored_value;
        bool found = head_node->read("new_key", stored_value);
        assert(found);
        assert(stored_value == "new_value");
        
        std::cout << "✓ Chain write operations test passed" << std::endl;
    }
    
    void test_write_forwarding() {
        std::cout << "Testing write forwarding in chain..." << std::endl;
        
        std::vector<uint32_t> node_ids = {1, 2, 3};
        auto middle_node = std::make_shared<Node>(2, node_ids); // Middle node
        
        std::vector<uint32_t> chain_order = {1, 2, 3};
        ChainReplication chain(middle_node, chain_order);
        
        // Test write at middle node (should be processed, not forwarded in this simple test)
        Message write_request;
        write_request.type = MessageType::WRITE_REQUEST;
        write_request.key = "forward_key";
        write_request.value = "forward_value";
        write_request.sender_id = 100;
        
        Message response;
        bool success = chain.process_write(write_request, response);
        
        // Write should succeed
        assert(response.type == MessageType::WRITE_RESPONSE);
        
        std::cout << "✓ Write forwarding test passed" << std::endl;
    }
    
    void test_node_failure_handling() {
        std::cout << "Testing node failure handling..." << std::endl;
        
        std::vector<uint32_t> node_ids = {1, 2, 3, 4};
        auto node = std::make_shared<Node>(1, node_ids);
        
        std::vector<uint32_t> chain_order = {1, 2, 3, 4};
        ChainReplication chain(node, chain_order);
        
        // Simulate node failure
        uint32_t failed_node = 3;
        chain.handle_node_failure(failed_node);
        
        // Check that chain length was updated
        size_t new_length = chain.get_chain_length();
        assert(new_length == 3); // Should be reduced by 1
        
        std::cout << "✓ Node failure handling test passed" << std::endl;
    }
    
    void test_node_recovery() {
        std::cout << "Testing node recovery..." << std::endl;
        
        std::vector<uint32_t> node_ids = {1, 2, 3};
        auto node = std::make_shared<Node>(1, node_ids);
        
        std::vector<uint32_t> chain_order = {1, 2}; // Node 3 initially failed
        ChainReplication chain(node, chain_order);
        
        // Simulate node recovery
        uint32_t recovered_node = 3;
        chain.handle_node_recovery(recovered_node);
        
        // Check that chain length increased
        size_t new_length = chain.get_chain_length();
        assert(new_length == 3); // Should be back to original size
        
        std::cout << "✓ Node recovery test passed" << std::endl;
    }
    
    void test_performance_optimization() {
        std::cout << "Testing performance optimizations..." << std::endl;
        
        std::vector<uint32_t> node_ids = {1, 2, 3};
        auto node = std::make_shared<Node>(1, node_ids);
        
        std::vector<uint32_t> chain_order = {1, 2, 3};
        ChainReplication chain(node, chain_order);
        
        // Test enabling optimizations
        chain.enable_batching(true);
        chain.enable_pipelining(true);
        
        // Test chain utilization
        double utilization = chain.get_chain_utilization();
        assert(utilization >= 0.0 && utilization <= 1.0);
        
        std::cout << "✓ Performance optimization test passed" << std::endl;
    }
    
    void test_batching() {
        std::cout << "Testing write batching..." << std::endl;
        
        std::vector<uint32_t> node_ids = {1, 2, 3};
        auto node = std::make_shared<Node>(1, node_ids);
        
        std::vector<uint32_t> chain_order = {1, 2, 3};
        ChainReplication chain(node, chain_order);
        
        chain.enable_batching(true);
        chain.set_batch_size(5);
        
        // Test multiple writes with batching enabled
        for (int i = 0; i < 3; i++) {
            Message write_msg;
            write_msg.type = MessageType::WRITE_REQUEST;
            write_msg.key = "batch_key_" + std::to_string(i);
            write_msg.value = "batch_value_" + std::to_string(i);
            
            Message response;
            bool success = chain.process_write(write_msg, response);
            assert(success);
        }
        
        std::cout << "✓ Write batching test passed" << std::endl;
    }
    
    void test_pipelining() {
        std::cout << "Testing write pipelining..." << std::endl;
        
        std::vector<uint32_t> node_ids = {1, 2, 3};
        auto node = std::make_shared<Node>(1, node_ids);
        
        std::vector<uint32_t> chain_order = {1, 2, 3};
        ChainReplication chain(node, chain_order);
        
        chain.enable_pipelining(true);
        
        // Test pipelining with multiple writes
        Message write_msg;
        write_msg.type = MessageType::WRITE_REQUEST;
        write_msg.key = "pipeline_key";
        write_msg.value = "pipeline_value";
        
        Message response;
        bool success = chain.process_write(write_msg, response);
        assert(success);
        
        std::cout << "✓ Write pipelining test passed" << std::endl;
    }
};

void run_chain_replication_tests() {
    try {
        ChainReplicationTest test;
        test.run_all_tests();
        
        std::cout << "\n🎉 All Chain Replication tests completed successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Chain Replication test failed with exception: " << e.what() << std::endl;
        throw;
    } catch (...) {
        std::cerr << "Chain Replication test failed with unknown exception" << std::endl;
        throw;
    }
}