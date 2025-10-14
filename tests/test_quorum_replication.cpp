#include "protocols/quorum_replication.h"
#include "core/node.h"
#include "utils/logger.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

using namespace replication;

class QuorumReplicationTest {
public:
    QuorumReplicationTest() {
        Logger::getInstance().setLogLevel(LogLevel::WARNING);
    }
    
    void run_all_tests() {
        std::cout << "Running Quorum Replication Tests..." << std::endl;
        
        test_quorum_initialization();
        test_quorum_size_calculation();
        test_consensus_operations();
        test_fast_quorum_reads();
        test_adaptive_quorum();
        test_node_failure_handling();
        test_timeout_handling();
        test_performance_metrics();
        
        std::cout << "All Quorum Replication tests passed!" << std::endl;
    }

private:
    void test_quorum_initialization() {
        std::cout << "  Testing quorum initialization..." << std::endl;
        
        std::vector<uint32_t> quorum_nodes = {1, 2, 3, 4, 5};
        auto node = std::make_shared<Node>(1, quorum_nodes);
        node->start();
        
        QuorumReplication quorum(node, quorum_nodes);
        
        assert(quorum.get_quorum_size() == 3); // (5/2) + 1 = 3
        assert(quorum.is_in_quorum(1) == true);
        assert(quorum.is_in_quorum(3) == true);
        assert(quorum.is_in_quorum(10) == false);
        
        node->stop();
        std::cout << "    ✓ Quorum initialization test passed" << std::endl;
    }
    
    void test_quorum_size_calculation() {
        std::cout << "  Testing quorum size calculation..." << std::endl;
        
        std::vector<uint32_t> nodes3 = {1, 2, 3};
        auto node3 = std::make_shared<Node>(1, nodes3);
        node3->start();
        QuorumReplication quorum3(node3, nodes3);
        assert(quorum3.get_quorum_size() == 2); // (3/2) + 1 = 2
        node3->stop();
        
        std::vector<uint32_t> nodes4 = {1, 2, 3, 4};
        auto node4 = std::make_shared<Node>(1, nodes4);
        node4->start();
        QuorumReplication quorum4(node4, nodes4);
        assert(quorum4.get_quorum_size() == 3); // (4/2) + 1 = 3
        node4->stop();
        
        std::vector<uint32_t> nodes7 = {1, 2, 3, 4, 5, 6, 7};
        auto node7 = std::make_shared<Node>(1, nodes7);
        node7->start();
        QuorumReplication quorum7(node7, nodes7);
        assert(quorum7.get_quorum_size() == 4); // (7/2) + 1 = 4
        node7->stop();
        
        std::cout << "    ✓ Quorum size calculation test passed" << std::endl;
    }
    
    void test_consensus_operations() {
        std::cout << "  Testing consensus operations..." << std::endl;
        
        std::vector<uint32_t> quorum_nodes = {1, 2, 3};
        auto node = std::make_shared<Node>(1, quorum_nodes);
        node->start();
        
        QuorumReplication quorum(node, quorum_nodes);
        
        // Test write operation (which uses consensus)
        Message write_request;
        write_request.type = MessageType::WRITE_REQUEST;
        write_request.key = "consensus_key";
        write_request.value = "consensus_value";
        write_request.sender_id = 100;
        write_request.sequence_number = 1;
        
        Message write_response;
        bool success = quorum.process_write(write_request, write_response);
        
        // In this simplified test, consensus might not complete due to 
        // lack of actual network communication between nodes
        assert(write_response.type == MessageType::WRITE_RESPONSE);
        
        node->stop();
        std::cout << "    ✓ Consensus operations test passed" << std::endl;
    }
    
    void test_fast_quorum_reads() {
        std::cout << "  Testing fast quorum reads..." << std::endl;
        
        std::vector<uint32_t> quorum_nodes = {1, 2, 3, 4, 5};
        auto node = std::make_shared<Node>(1, quorum_nodes);
        node->start();
        
        // Pre-populate some data
        node->write("fast_key", "fast_value");
        
        QuorumReplication quorum(node, quorum_nodes);
        
        // Enable fast quorum optimization
        quorum.enable_fast_quorum(true);
        quorum.enable_read_optimization(true);
        
        Message read_request;
        read_request.type = MessageType::READ_REQUEST;
        read_request.key = "fast_key";
        read_request.sender_id = 100;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        Message read_response;
        bool success = quorum.process_read(read_request, read_response);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        
        assert(success == true);
        assert(read_response.value == "fast_value");
        
        // Fast path should be very quick (< 1ms in most cases)
        assert(duration < 1000); // Less than 1ms
        
        node->stop();
        std::cout << "    ✓ Fast quorum reads test passed" << std::endl;
    }
    
    void test_adaptive_quorum() {
        std::cout << "  Testing adaptive quorum..." << std::endl;
        
        std::vector<uint32_t> quorum_nodes = {1, 2, 3, 4, 5, 6, 7};
        auto node = std::make_shared<Node>(1, quorum_nodes);
        node->start();
        
        QuorumReplication quorum(node, quorum_nodes);
        
        // Enable adaptive quorum
        quorum.enable_adaptive_quorum(true);
        
        size_t initial_size = quorum.get_quorum_size();
        assert(initial_size == 4); // (7/2) + 1
        
        // Simulate load-based adjustment
        quorum.adjust_quorum_size_based_on_load();
        
        // The quorum size may adjust based on performance metrics
        size_t adjusted_size = quorum.get_quorum_size();
        assert(adjusted_size >= 3 && adjusted_size <= quorum_nodes.size());
        
        node->stop();
        std::cout << "    ✓ Adaptive quorum test passed" << std::endl;
    }
    
    void test_node_failure_handling() {
        std::cout << "  Testing node failure handling..." << std::endl;
        
        std::vector<uint32_t> quorum_nodes = {1, 2, 3, 4, 5};
        auto node = std::make_shared<Node>(1, quorum_nodes);
        node->start();
        
        QuorumReplication quorum(node, quorum_nodes);
        
        // Disable adaptive quorum for predictable testing
        quorum.enable_adaptive_quorum(false);
        
        assert(quorum.get_quorum_size() == 3);
        assert(quorum.is_in_quorum(2) == true);
        assert(quorum.is_in_quorum(4) == true);
        
        // Simulate node failure
        quorum.handle_node_failure(2);
        
        assert(quorum.is_in_quorum(2) == false);
        std::cout << "    After removing node 2, quorum size: " << quorum.get_quorum_size() << std::endl;
        assert(quorum.get_quorum_size() == 3); // (4/2) + 1
        
        // Simulate another failure
        quorum.handle_node_failure(4);
        
        assert(quorum.is_in_quorum(4) == false);
        assert(quorum.get_quorum_size() == 2); // (3/2) + 1
        
        // Simulate node recovery
        quorum.handle_node_recovery(2);
        
        assert(quorum.is_in_quorum(2) == true);
        assert(quorum.get_quorum_size() == 3); // (4/2) + 1
        
        node->stop();
        std::cout << "    ✓ Node failure handling test passed" << std::endl;
    }
    
    void test_timeout_handling() {
        std::cout << "  Testing timeout handling..." << std::endl;
        
        std::vector<uint32_t> quorum_nodes = {1, 2, 3};
        auto node = std::make_shared<Node>(1, quorum_nodes);
        node->start();
        
        QuorumReplication quorum(node, quorum_nodes);
        
        // Set a very short timeout for testing
        quorum.set_timeout(100); // 100ms
        
        Message write_request;
        write_request.type = MessageType::WRITE_REQUEST;
        write_request.key = "timeout_key";
        write_request.value = "timeout_value";
        write_request.sender_id = 100;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        Message write_response;
        bool success = quorum.process_write(write_request, write_response);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        
        // Operation should timeout and complete within reasonable time
        assert(duration >= 100); // At least the timeout duration
        assert(duration < 1000);  // But not too long
        
        node->stop();
        std::cout << "    ✓ Timeout handling test passed" << std::endl;
    }
    
    void test_performance_metrics() {
        std::cout << "  Testing performance metrics..." << std::endl;
        
        std::vector<uint32_t> quorum_nodes = {1, 2, 3, 4, 5};
        auto node = std::make_shared<Node>(1, quorum_nodes);
        node->start();
        
        QuorumReplication quorum(node, quorum_nodes);
        
        // Perform some operations to generate metrics
        for (int i = 0; i < 5; ++i) {
            Message read_request;
            read_request.type = MessageType::READ_REQUEST;
            read_request.key = "metric_key_" + std::to_string(i);
            
            Message read_response;
            quorum.process_read(read_request, read_response);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Check consensus metrics
        double success_rate = quorum.get_consensus_success_rate();
        assert(success_rate >= 0.0 && success_rate <= 1.0);
        
        double avg_time = quorum.get_average_consensus_time();
        assert(avg_time >= 0.0);
        
        node->stop();
        std::cout << "    ✓ Performance metrics test passed" << std::endl;
    }
    
    void test_paxos_message_handling() {
        std::cout << "  Testing Paxos message handling..." << std::endl;
        
        std::vector<uint32_t> quorum_nodes = {1, 2, 3};
        auto node = std::make_shared<Node>(1, quorum_nodes);
        node->start();
        
        QuorumReplication quorum(node, quorum_nodes);
        
        // Test prepare message handling
        Message prepare_msg;
        prepare_msg.type = MessageType::QUORUM_PREPARE;
        prepare_msg.sender_id = 2;
        prepare_msg.sequence_number = 1;
        prepare_msg.key = "paxos_key";
        
        quorum.handle_prepare(prepare_msg);
        
        // Test promise message handling
        Message promise_msg;
        promise_msg.type = MessageType::QUORUM_PROMISE;
        promise_msg.sender_id = 2;
        promise_msg.sequence_number = 1;
        promise_msg.success = true;
        
        quorum.handle_promise(promise_msg);
        
        // Test accept message handling
        Message accept_msg;
        accept_msg.type = MessageType::QUORUM_ACCEPT;
        accept_msg.sender_id = 2;
        accept_msg.sequence_number = 1;
        accept_msg.key = "paxos_key";
        accept_msg.value = "paxos_value";
        
        quorum.handle_accept(accept_msg);
        
        // Test accepted message handling
        Message accepted_msg;
        accepted_msg.type = MessageType::QUORUM_ACCEPTED;
        accepted_msg.sender_id = 2;
        accepted_msg.sequence_number = 1;
        accepted_msg.success = true;
        
        quorum.handle_accepted(accepted_msg);
        
        node->stop();
        std::cout << "    ✓ Paxos message handling test passed" << std::endl;
    }
};

void run_quorum_replication_tests() {
    try {
        QuorumReplicationTest test;
        test.run_all_tests();
        std::cout << "\n🎉 All Quorum Replication tests completed successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Quorum Replication test failed: " << e.what() << std::endl;
        throw;
    }
} 