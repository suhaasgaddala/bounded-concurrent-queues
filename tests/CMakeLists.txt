find_package(Threads REQUIRED)

set(orbitqueue_test_sources
    test_main.cpp
    fixed_message_tests.cpp
    blocking_queue_tests.cpp
    mpmc_queue_tests.cpp
    spsc_queue_tests.cpp
    spmc_multicast_queue_tests.cpp
    versioned_spmc_queue_tests.cpp)

if(ORBITQUEUE_BUILD_BENCHMARKS)
    list(APPEND orbitqueue_test_sources benchmark_support_tests.cpp)
endif()

add_executable(orbitqueue_tests ${orbitqueue_test_sources})
target_link_libraries(orbitqueue_tests PRIVATE orbitqueue Threads::Threads)

add_library(orbitqueue_header_self_sufficiency OBJECT
    header_self_sufficiency/blocking_queue.cpp
    header_self_sufficiency/fixed_message.cpp
    header_self_sufficiency/mpmc_queue.cpp
    header_self_sufficiency/result.cpp
    header_self_sufficiency/spmc_multicast_queue.cpp
    header_self_sufficiency/spsc_queue.cpp
    header_self_sufficiency/versioned_spmc_queue.cpp)
target_link_libraries(orbitqueue_header_self_sufficiency PRIVATE orbitqueue)

if(ORBITQUEUE_BUILD_BENCHMARKS)
    target_include_directories(orbitqueue_tests PRIVATE "${PROJECT_SOURCE_DIR}/benchmarks")
    target_compile_definitions(orbitqueue_tests PRIVATE
        ORBITQUEUE_TEST_BENCHMARK_SUPPORT=1)
endif()

add_test(NAME orbitqueue_tests COMMAND orbitqueue_tests)

add_test(
    NAME orbitqueue_downstream_package
    COMMAND "${CMAKE_COMMAND}"
        "-DORBITQUEUE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DORBITQUEUE_BINARY_DIR=${PROJECT_BINARY_DIR}"
        "-DORBITQUEUE_TEST_CONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/run_downstream_test.cmake")
