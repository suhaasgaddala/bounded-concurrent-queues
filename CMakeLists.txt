cmake_minimum_required(VERSION 3.20)

project(OrbitQueue VERSION 2.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(ORBITQUEUE_BUILD_TESTS "Build OrbitQueue tests" ON)
option(ORBITQUEUE_BUILD_BENCHMARKS "Build OrbitQueue benchmarks" ON)
option(ORBITQUEUE_BUILD_STRESS "Build OrbitQueue deterministic stress runner" ON)
option(ORBITQUEUE_ENABLE_BOOST_BENCHMARKS
    "Enable optional Boost.Lockfree benchmark scenarios" OFF)
option(ORBITQUEUE_ENABLE_WARNINGS "Enable strong compiler warnings" ON)
option(ORBITQUEUE_ENABLE_SANITIZERS "Enable compiler sanitizers" OFF)
set(ORBITQUEUE_SANITIZERS "address,undefined" CACHE STRING
    "Comma-separated sanitizers used when ORBITQUEUE_ENABLE_SANITIZERS is ON")

add_library(orbitqueue INTERFACE)
add_library(OrbitQueue::orbitqueue ALIAS orbitqueue)
target_compile_features(orbitqueue INTERFACE cxx_std_20)
target_include_directories(orbitqueue INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(ORBITQUEUE_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(orbitqueue INTERFACE /W4 /permissive-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(orbitqueue INTERFACE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endif()

if(ORBITQUEUE_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "ORBITQUEUE_ENABLE_SANITIZERS is not supported with MSVC")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(orbitqueue INTERFACE
            -fsanitize=${ORBITQUEUE_SANITIZERS} -fno-omit-frame-pointer)
        target_link_options(orbitqueue INTERFACE
            -fsanitize=${ORBITQUEUE_SANITIZERS})
    else()
        message(FATAL_ERROR "Sanitizers require a Clang or GCC-compatible compiler")
    endif()
endif()

if(ORBITQUEUE_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests)
endif()

if(ORBITQUEUE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(ORBITQUEUE_BUILD_STRESS)
    add_subdirectory(stress)
endif()

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/OrbitQueueConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OrbitQueueConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/OrbitQueueConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OrbitQueue")

install(TARGETS orbitqueue EXPORT OrbitQueueTargets)
install(DIRECTORY include/orbitqueue
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES LICENSE
    DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(EXPORT OrbitQueueTargets
    FILE OrbitQueueTargets.cmake
    NAMESPACE OrbitQueue::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OrbitQueue")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/OrbitQueueConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OrbitQueueConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/OrbitQueue")
