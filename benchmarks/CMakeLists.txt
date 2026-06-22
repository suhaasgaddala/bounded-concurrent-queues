find_package(Threads REQUIRED)

add_executable(orbitqueue_benchmark queue_benchmark.cpp)
target_link_libraries(orbitqueue_benchmark PRIVATE orbitqueue Threads::Threads)
target_include_directories(orbitqueue_benchmark PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(ORBITQUEUE_ENABLE_BOOST_BENCHMARKS)
    find_path(ORBITQUEUE_BOOST_LOCKFREE_INCLUDE_DIR
        NAMES boost/lockfree/queue.hpp)

    if(ORBITQUEUE_BOOST_LOCKFREE_INCLUDE_DIR)
        target_compile_definitions(orbitqueue_benchmark PRIVATE
            ORBITQUEUE_HAVE_BOOST_LOCKFREE=1)
        target_include_directories(orbitqueue_benchmark PRIVATE
            "${ORBITQUEUE_BOOST_LOCKFREE_INCLUDE_DIR}")
        message(STATUS "OrbitQueue Boost.Lockfree benchmarks enabled")
    else()
        message(WARNING
            "ORBITQUEUE_ENABLE_BOOST_BENCHMARKS is ON, but "
            "boost/lockfree/queue.hpp was not found; Boost scenarios are disabled")
    endif()
endif()

if(ORBITQUEUE_BUILD_TESTS)
    add_test(NAME orbitqueue_benchmark_smoke COMMAND orbitqueue_benchmark 10)
endif()
