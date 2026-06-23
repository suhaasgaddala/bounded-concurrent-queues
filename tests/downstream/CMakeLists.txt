cmake_minimum_required(VERSION 3.20)

project(OrbitQueueDownstream LANGUAGES CXX)

find_package(OrbitQueue CONFIG REQUIRED)

add_executable(orbitqueue_downstream main.cpp)
target_link_libraries(orbitqueue_downstream PRIVATE OrbitQueue::orbitqueue)

add_executable(bounded_concurrent_queues_downstream main.cpp)
target_link_libraries(
    bounded_concurrent_queues_downstream
    PRIVATE BoundedConcurrentQueues::orbitqueue)

enable_testing()
add_test(NAME orbitqueue_downstream COMMAND orbitqueue_downstream)
add_test(
    NAME bounded_concurrent_queues_downstream
    COMMAND bounded_concurrent_queues_downstream)
