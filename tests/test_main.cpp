#include "utils/logger.h"
#include <iostream>
#include <exception>

using namespace replication;

// Forward declarations of test functions
void run_chain_replication_tests();
void run_quorum_replication_tests(); 
void run_hybrid_protocol_tests();
void run_performance_tests();

int main() {
    // Initialize logging
    Logger& logger = Logger::getInstance();
    logger.setLogLevel(LogLevel::INFO);
    
    std::cout << "🧪 Running Hybrid Chain-Quorum Replication Protocol Test Suite" << std::endl;
    std::cout << "================================================================" << std::endl;
    
    try {
        std::cout << "\n[1/4] Chain Replication Tests" << std::endl;
        std::cout << "------------------------------" << std::endl;
        run_chain_replication_tests();
        
        std::cout << "\n[2/4] Quorum Replication Tests" << std::endl;
        std::cout << "-------------------------------" << std::endl;
        run_quorum_replication_tests();
        
        std::cout << "\n[3/4] Hybrid Protocol Tests" << std::endl;
        std::cout << "----------------------------" << std::endl;
        run_hybrid_protocol_tests();
        
        std::cout << "\n[4/4] Performance Tests" << std::endl;
        std::cout << "------------------------" << std::endl;
        run_performance_tests();
        
        std::cout << "\n🎉 ALL TESTS PASSED! 🎉" << std::endl;
        std::cout << "=====================================" << std::endl;
        std::cout << "The Hybrid Chain-Quorum Replication Protocol" << std::endl;
        std::cout << "has been successfully tested and verified!" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n❌ TEST SUITE FAILED!" << std::endl;
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\n❌ TEST SUITE FAILED!" << std::endl;
        std::cerr << "Unknown error occurred" << std::endl;
        return 1;
    }
}