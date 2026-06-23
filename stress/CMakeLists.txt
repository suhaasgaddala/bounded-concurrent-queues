find_package(Threads REQUIRED)

add_executable(orbitqueue_stress queue_stress.cpp)
target_link_libraries(orbitqueue_stress PRIVATE orbitqueue Threads::Threads)

if(ORBITQUEUE_BUILD_TESTS)
    add_test(NAME orbitqueue_stress_spsc_smoke
        COMMAND orbitqueue_stress
            --queue spsc --seed 101 --duration-ms 500 --iterations 2000
            --producers 1 --consumers 1 --payload-size 32 --capacity 7)
    add_test(NAME orbitqueue_stress_blocking_smoke
        COMMAND orbitqueue_stress
            --queue blocking --seed 202 --duration-ms 500 --iterations 2000
            --producers 3 --consumers 3 --payload-size 48 --capacity 17)
    add_test(NAME orbitqueue_stress_spmc_smoke
        COMMAND orbitqueue_stress
            --queue spmc --seed 303 --duration-ms 500 --iterations 2000
            --producers 1 --consumers 3 --payload-size 64 --capacity 31)
    add_test(NAME orbitqueue_stress_versioned_spmc_smoke
        COMMAND orbitqueue_stress
            --queue versioned_spmc --seed 305 --duration-ms 500 --iterations 2000
            --producers 1 --consumers 3 --payload-size 64 --capacity 31)
    add_test(NAME orbitqueue_stress_mpmc_smoke
        COMMAND orbitqueue_stress
            --queue mpmc --seed 404 --duration-ms 500 --iterations 2000
            --producers 3 --consumers 3 --payload-size 80 --capacity 64)
endif()
