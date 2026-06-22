find_package(Threads REQUIRED)

add_executable(orbitqueue_benchmark queue_benchmark.cpp)
target_link_libraries(orbitqueue_benchmark PRIVATE orbitqueue Threads::Threads)
target_include_directories(orbitqueue_benchmark PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE orbitqueue_git_result
    OUTPUT_VARIABLE orbitqueue_git_commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT orbitqueue_git_result EQUAL 0 OR orbitqueue_git_commit STREQUAL "")
    set(orbitqueue_git_commit "unknown")
endif()

target_compile_definitions(orbitqueue_benchmark PRIVATE
    "ORBITQUEUE_BENCHMARK_BUILD_TYPE=\"$<CONFIG>\""
    "ORBITQUEUE_BENCHMARK_COMPILER=\"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\""
    "ORBITQUEUE_BENCHMARK_GIT_COMMIT=\"${orbitqueue_git_commit}\"")

if(ORBITQUEUE_ENABLE_BOOST_BENCHMARKS)
    find_path(ORBITQUEUE_BOOST_LOCKFREE_INCLUDE_DIR
        NAMES boost/lockfree/queue.hpp)

    if(ORBITQUEUE_BOOST_LOCKFREE_INCLUDE_DIR)
        target_compile_definitions(orbitqueue_benchmark PRIVATE
            ORBITQUEUE_HAVE_BOOST_LOCKFREE=1)
        target_include_directories(orbitqueue_benchmark PRIVATE
            "${ORBITQUEUE_BOOST_LOCKFREE_INCLUDE_DIR}")
        message(STATUS "Bounded queue Boost.Lockfree benchmarks enabled")
    else()
        message(WARNING
            "ORBITQUEUE_ENABLE_BOOST_BENCHMARKS is ON, but "
            "boost/lockfree/queue.hpp was not found; Boost scenarios are disabled")
    endif()
endif()

if(ORBITQUEUE_BUILD_TESTS)
    add_test(NAME orbitqueue_benchmark_smoke
        COMMAND orbitqueue_benchmark
            --duration-ms 10 --warmup-ms 0 --trials 1
            --capacity 64 --payload-size 32 --consumers 1,3 --queue all)
endif()
