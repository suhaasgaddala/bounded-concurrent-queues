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

## Queue types

- `BlockingQueue<T>`: bounded mutex-based FIFO with close and drain behavior.
- `MPMCQueue<N>`: bounded multi-producer, multi-consumer non-blocking
  work-sharing queue with fixed-size payloads and close/drain behavior.
- `SPSCQueue<N>`: bounded single-producer, single-consumer work-sharing FIFO
  with fixed-size payloads and no overwrite.
- `SPMCMulticastQueue<N>`: single-producer multicast ring where each registered
  consumer has an independent cursor. A mutex protects publication and reads;
  slow consumers detect overwritten history.

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

## Benchmark

```sh
./build/benchmarks/orbitqueue_benchmark 1000
```

The optional argument is duration in milliseconds. Each queue emits one JSON
line per scenario. The default run includes SPSC with one consumer plus
multicast and blocking work-sharing scenarios with 1, 3, and 10 consumers.
SPSC is never run with multiple consumers because that violates its contract.

An optional Boost.Lockfree work-sharing matrix can be enabled without making
Boost a core dependency:

```sh
cmake -S . -B build-boost \
  -DCMAKE_BUILD_TYPE=Release \
  -DORBITQUEUE_ENABLE_BOOST_BENCHMARKS=ON
cmake --build build-boost --parallel
./build-boost/benchmarks/orbitqueue_benchmark 1000
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

Tests cover boundaries, FIFO order, wraparound, close wakeups, concurrent SPSC
sequence integrity, independent multicast cursors, and lag detection. The SPSC
implementation uses acquire/release atomics under its stated one-producer,
one-consumer contract. The multicast implementation deliberately uses a mutex
to avoid payload races during overwrite. No lock-free progress guarantee is
made.

See [docs/memory_model.md](docs/memory_model.md) for the synchronization and
happens-before rationale, [docs/correctness_strategy.md](docs/correctness_strategy.md)
for the validation layers, and [docs/stress_testing.md](docs/stress_testing.md)
for deterministic stress reproduction.

## Roadmap

- Expand randomized and long-duration sanitizer testing.
- Record benchmark environment metadata and repeated-trial statistics.
- Investigate faster multicast synchronization only behind equivalent
  correctness tests.

Detailed contracts are in [docs/queue_contracts.md](docs/queue_contracts.md).
