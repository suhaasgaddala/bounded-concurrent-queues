# OrbitQueue v2

OrbitQueue is a C++20 concurrency research library for fixed-size in-memory
queues. It focuses on explicit delivery semantics, correctness testing, and
reproducible benchmarking.

OrbitQueue v2 is an experimental foundation for studying bounded queue
contracts. It is not a production-readiness claim, a formal verification, or
evidence that any implementation is the fastest available.

It is the supported successor to the original OrbitQueue prototype. V2
preserves the useful SPSC, multicast, blocking-queue, and benchmark concepts
while replacing unsafe raw APIs, ambiguous delivery semantics, machine-specific
build assumptions, and unsupported performance claims.

See [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md) for the exhaustive repository,
API, validation, risk, and roadmap snapshot.

## Why OrbitQueue exists

Concurrent queues are meaningful only when producer ownership, consumer
ownership, delivery semantics, overflow behavior, and shutdown are explicit.
OrbitQueue puts those contracts ahead of throughput numbers. It pairs narrow
queue APIs with deterministic seed-based stress tests, sanitizer-ready builds,
and benchmarks that keep multicast reads distinct from work-sharing pops.

V2 is the maintained successor to the legacy prototype. It preserves useful
experiments and historical artifacts while excluding the old unsafe raw-memory
APIs and unsupported performance claims.

## Current queues

| Queue | Concurrency and delivery | Capacity and synchronization |
| --- | --- | --- |
| `BlockingQueue<T>` | Multiple producers and consumers; work sharing | Bounded; mutex and condition variables; blocking and try operations |
| `SPSCQueue<N>` | Exactly one producer and one consumer; work sharing | Bounded fixed-message ring; acquire/release atomics |
| `SPMCMulticastQueue<N>` | One producer and registered independent consumers; multicast | Bounded retained history; mutex-protected publication and reads; detectable lag |
| `MPMCQueue<N>` | Multiple producers and consumers; work sharing | Bounded power-of-two sequence-cell ring; mutex-free, try-only operations |

The public payload APIs use `std::span` and reject oversized messages. Queue
status and sequence information are returned explicitly.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CMake 3.20 and a C++20 compiler are required. The core library has no mandatory
third-party dependencies.

Useful options:

```sh
-DORBITQUEUE_BUILD_TESTS=ON
-DORBITQUEUE_BUILD_BENCHMARKS=ON
-DORBITQUEUE_BUILD_STRESS=ON
-DORBITQUEUE_ENABLE_BOOST_BENCHMARKS=OFF
-DORBITQUEUE_ENABLE_WARNINGS=ON
-DORBITQUEUE_ENABLE_SANITIZERS=ON
-DORBITQUEUE_SANITIZERS=address,undefined
```

For ThreadSanitizer, use a separate build with
`-DORBITQUEUE_ENABLE_SANITIZERS=ON -DORBITQUEUE_SANITIZERS=thread` because TSan
cannot generally be combined with ASan.

## Install and consume

Install OrbitQueue to a prefix:

```sh
cmake --install build --prefix /path/to/orbitqueue
```

Downstream CMake projects can then use the exported target:

```cmake
find_package(OrbitQueue 2 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE OrbitQueue::orbitqueue)
```

Pass the installation prefix through `CMAKE_PREFIX_PATH` when it is outside a
standard system location. The test suite includes an isolated downstream build
that validates this workflow.

## Deterministic stress testing

```sh
./build/stress/orbitqueue_stress \
  --queue all --seed 12345 --duration-ms 250 --iterations 10000
```

The runner validates sequence-bearing, checksummed payloads for SPSC, blocking
MPMC, retained-history SPMC, and `MPMCQueue`. It reports the complete
configuration and seed needed to reproduce a failure. Longer and targeted
commands are documented in [docs/stress_testing.md](docs/stress_testing.md).

## Benchmark

```sh
./build/benchmarks/orbitqueue_benchmark \
  --duration-ms 1000 --warmup-ms 100 --trials 5
```

Each queue emits one JSON line per trial with validation counters, throughput,
build type, compiler, commit, timestamp, and a delivery-semantics note. The
default run includes the standard SPSC, SPMC, blocking, and MPMC matrices.
SPSC remains 1/1 because any other topology violates its contract.

An optional Boost.Lockfree work-sharing matrix can be enabled without making
Boost a core dependency:

```sh
cmake -S . -B build-boost \
  -DCMAKE_BUILD_TYPE=Release \
  -DORBITQUEUE_ENABLE_BOOST_BENCHMARKS=ON
cmake --build build-boost --parallel
./build-boost/benchmarks/orbitqueue_benchmark \
  --queue boost --duration-ms 1000 --trials 5
```

If the Boost.Lockfree header is unavailable, CMake warns and builds every
non-Boost target and scenario normally. SPSC unique pops, blocking/Boost
work-sharing pops, and multicast aggregate reads have different meanings and
must not be ranked as equivalent work. See
[docs/benchmarking.md](docs/benchmarking.md).

Historical v1 context and explicitly labeled artifacts are under
[docs/legacy](docs/legacy/README.md). The full migration disposition is in the
[v1 parity audit](docs/v1_parity_audit.md).

## Correctness status

Tests cover boundaries, FIFO order, wraparound, undersized retries, close and
drain behavior, concurrent work sharing, independent multicast cursors, and lag
detection. The stress runner adds deterministic high-volume payload validation.
The SPSC implementation uses acquire/release atomics under its stated contract;
the multicast implementation deliberately uses a mutex, while MPMC transfers
cell ownership with acquire/release generation sequences. MPMC is described as
mutex-free, not lock-free or wait-free.

See [docs/memory_model.md](docs/memory_model.md) for the synchronization and
happens-before rationale, [docs/correctness_strategy.md](docs/correctness_strategy.md)
for the validation layers, and [docs/stress_testing.md](docs/stress_testing.md)
for deterministic stress reproduction.
The [MPMC design note](docs/mpmc_queue.md) documents its cell protocol,
power-of-two capacity requirement, consuming short-read policy, and limits.

## Roadmap

- Automate longer randomized stress and sanitizer schedules in CI.
- Add benchmark data retention and external statistical analysis tooling.
- Investigate faster multicast synchronization only behind equivalent
  correctness tests.
- Review the mutex-free MPMC progress model independently and investigate
  model checking before considering stronger guarantees or a close protocol.

Detailed contracts are in [docs/queue_contracts.md](docs/queue_contracts.md).
