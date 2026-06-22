#include <exception>
#include <iostream>

void run_fixed_message_tests();
void run_blocking_queue_tests();
void run_mpmc_queue_tests();
void run_spsc_queue_tests();
void run_spmc_multicast_queue_tests();
#if defined(ORBITQUEUE_TEST_BENCHMARK_SUPPORT)
void run_benchmark_support_tests();
#endif

int main() {
    try {
        run_fixed_message_tests();
        run_blocking_queue_tests();
        run_mpmc_queue_tests();
        run_spsc_queue_tests();
        run_spmc_multicast_queue_tests();
#if defined(ORBITQUEUE_TEST_BENCHMARK_SUPPORT)
        run_benchmark_support_tests();
#endif
        std::cout << "All OrbitQueue tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OrbitQueue test failure: " << error.what() << '\n';
        return 1;
    }
}
