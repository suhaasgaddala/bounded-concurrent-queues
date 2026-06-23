# Bounded Concurrent Queues for C++20: Project Context

> Detailed repository snapshot updated 2026-06-22. Public headers and tests
> remain the source of truth.

## 1. Purpose and Evidence Model

Bounded Concurrent Queues for C++20 is a header-only concurrency research
library for bounded, in-memory queues. It makes queue ownership, delivery,
overflow, ordering, and shutdown contracts explicit before using performance
results.

This document uses four evidence categories:

- **Contract:** caller-visible behavior within documented usage rules.
- **Implementation:** the mechanism currently used to realize that contract.
- **Validation:** behavior exercised by tests, stress, sanitizers, or packaging.
- **Limitation:** absent behavior or a property not established by evidence.

When statements disagree, use this order: public headers, executable tests,
focused contract documents, this snapshot, then supporting documentation.

The library is experimental. It is not claimed production-ready, formally
verified, wait-free, officially lock-free, or fastest. Mutex-free describes the
new MPMC implementation; it is not used as a synonym for a progress guarantee.

Release line: `v0.1.0` is the initial public release tag. Current `main`
prepares `v0.1.1`, which carries post-release cache-layout consistency,
sanitizer CI visibility, and package metadata alignment while retaining the
existing compatibility names.

## 2. Current Queue Families

| Type | Producer/consumer model | Delivery | Boundary behavior | Synchronization |
| --- | --- | --- | --- | --- |
| `BlockingQueue<T>` | Multiple producers and consumers | Work sharing | Bounded; blocking or `full`/`empty`; close and drain | Mutex and condition variables |
| `SPSCQueue<N>` | Exactly one producer and one consumer | Work sharing | Bounded; `full`/`empty`; no close | Acquire/release head and tail atomics |
| `SPMCMulticastQueue<N>` | One producer and registered consumers | Multicast retained history | Overwrites old history; consumers detect lag | One mutex across publication and copy |
| `MPMCQueue<N>` | Multiple producers and consumers | Work sharing | Power-of-two bounded; try-only `full`/`empty`; no close | Sequence-numbered cells and CAS position claims; no mutex |

All fixed-payload queues use `std::span`, reject oversized messages, hide slot
storage, and return explicit status, byte-count, and sequence results.

## 3. Goals and Non-Goals

Current goals:

- keep the core dependency-free and package-consumable;
- prevent raw slot/index protocols from escaping into caller code;
- preserve bounded allocation and predictable boundary statuses;
- validate concurrent delivery with deterministic-input payloads;
- explain every synchronization mechanism and its evidence limits;
- compare only scenarios whose delivery units are clearly labeled.

Current non-goals:

- persistent, inter-process, distributed, or network queues;
- dynamic payload growth beyond a compile-time maximum;
- a stable ABI or compatibility with raw-slot and caller-managed-index APIs;
- cancellation, timed waits, or a unified close protocol;
- defined position/sequence wraparound behavior;
- formal linearizability or progress proofs;
- performance rankings or regression thresholds.

## 4. Repository State

| Attribute | Value |
| --- | --- |
| GitHub repository | `https://github.com/suhaasgaddala/bounded-concurrent-queues` |
| Default branch | `main` |
| Project version | `0.1.1` |
| Release state | Preparing follow-up `v0.1.1`; `v0.1.0` is the initial public tag |
| CMake project identifier | `BoundedConcurrentQueues` |
| Core form | Header-only CMake interface target |
| Required language | C++20 |
| Minimum CMake | 3.20 |
| Mandatory third-party dependencies | None |

## 5. Repository Map

```text
.
|-- .github/workflows/
|   |-- ci.yml
|   `-- verification.yml
|-- CMakeLists.txt
|-- README.md
|-- PROJECT_CONTEXT.md
|-- benchmarks/
|   |-- CMakeLists.txt
|   |-- benchmark_support.h
|   `-- queue_benchmark.cpp
|-- cmake/OrbitQueueConfig.cmake.in
|-- docs/
|   |-- architecture.md
|   |-- assets/
|   |-- benchmarking.md
|   |-- correctness_strategy.md
|   |-- design_decisions.md
|   |-- design_explorations.md
|   |-- memory_model.md
|   |-- mpmc_queue.md
|   |-- queue_contracts.md
|   |-- research_motivation.md
|   `-- stress_testing.md
|-- include/orbitqueue/
|   |-- detail/
|   |   `-- cache_layout.h
|   |-- blocking_queue.h
|   |-- fixed_message.h
|   |-- mpmc_queue.h
|   |-- result.h
|   |-- spmc_multicast_queue.h
|   |-- spsc_queue.h
|   `-- version.h
|-- stress/
|   |-- CMakeLists.txt
|   `-- queue_stress.cpp
|-- verification/
|   |-- genmc/
|   |-- tla/
|   |-- claims.md
|   |-- results.md
|   |-- run_genmc.sh
|   |-- run_negative_controls.sh
|   `-- run_tlc.sh
`-- tests/
    |-- CMakeLists.txt
    |-- benchmark_support_tests.cpp
    |-- blocking_queue_tests.cpp
    |-- fixed_message_tests.cpp
    |-- mpmc_queue_tests.cpp
    |-- spmc_multicast_queue_tests.cpp
    |-- spsc_queue_tests.cpp
    |-- test_main.cpp
    |-- test_support.h
    |-- header_self_sufficiency/...
    `-- downstream/...
```

## 6. Shared Payload and Result Model

`QueueStatus` includes ordinary boundary states such as `success`, `empty`,
`full`, `closed`, `message_too_large`, `consumer_lagged`, and `overwritten`.
`WriteResult` contains status and logical sequence. `ReadResult` additionally
contains `bytes_read`.

`FixedMessage<MaxPayloadSize>` owns an inline byte array, active size, and
logical sequence. `assign` rejects oversized input before mutation. `copy_to`
returns `message_too_large`, zero bytes, and the stored sequence when its
destination is short. The queue decides whether its cursor or claimed position
advances after that result.

Successful fixed-payload queue sequences start at one. Sequence zero means no
message was assigned to the unsuccessful operation. Counter exhaustion is not
handled by the current queue implementations.

## 7. BlockingQueue Contract

`BlockingQueue<T>` is the conservative generic MPMC baseline. One mutex protects
an STL queue and closure state. Condition variables wait for non-empty and
non-full predicates.

- Capacity zero throws `std::invalid_argument`.
- `push` and `pop` block until progress or closure.
- `try_push` and `try_pop` report boundaries without waiting.
- `close` is idempotent and wakes producer and consumer waiters.
- Closure rejects new pushes but preserves queued items for drain.
- Empty closed blocking pops report closure.

User-defined copy, move, assignment, and allocation can throw. There are no
timed waits, stop tokens, reopen, or clear operations.

## 8. SPSCQueue Contract and Memory Ordering

`SPSCQueue<N>` is a bounded work-sharing ring for exactly one producer and one
consumer. Additional producers or consumers violate its contract.

The producer owns `head_`, loads it relaxed, and acquires consumer-owned
`tail_` before deciding whether capacity exists. It fills a `FixedMessage`, then
publishes the incremented head with release ordering. The consumer acquires the
head before copying payload bytes and releases its incremented tail after the
copy, allowing safe slot reuse.

`head_` and `tail_` use the shared destructive-interference-size cache-layout
utility and instantiate `PaddedAtomic<std::uint64_t>`. Compile-time assertions
verify the exact padded atomic type. This reduces false-sharing risk between
producer and consumer counters, but it is not a correctness requirement or
performance guarantee.

Full never overwrites unread data. A short destination does not advance the
single consumer tail, so retry with a larger span is supported. `empty()` and
`full()` are advisory concurrent observations, not reservations. The queue has
no blocking or close operation.

## 9. SPMCMulticastQueue Contract

`SPMCMulticastQueue<N>` is retained-history multicast, not work sharing. One
producer publishes monotonically sequenced messages. Each move-only consumer
handle has an independent next-sequence cursor; consumers registered after
prior publications start at the next publication and do not replay history.

One mutex covers publication, generation inspection, lag recovery, and payload
copy. This prevents a producer from rewriting ordinary bytes while a consumer
copies them. Publication never waits for a slow consumer. When a cursor falls
behind retained history, the handle reports `consumer_lagged`, moves to the
oldest retained sequence, and can continue.

A short destination leaves that consumer's cursor unchanged. Consumer handles
contain a non-owning queue pointer and must not outlive the queue. One handle
must not be called concurrently. There is no close or blocking operation.

## 10. MPMCQueue Contract

`MPMCQueue<N>` is a bounded, try-only, fixed-payload MPMC work-sharing ring
adapted from Dmitry Vyukov's array-based sequence-cell algorithm.

### 10.1 Construction and storage

- Capacity must be a power of two greater than one.
- Invalid capacities throw `std::invalid_argument`.
- Construction allocates one `Cell[]`; try operations allocate nothing.
- Every cell holds an atomic `std::size_t` generation and one `FixedMessage<N>`.
- `mask = capacity - 1` maps positions to cells.
- Cells and hot enqueue/dequeue position counters use the shared
  destructive-interference-size cache-layout utility, which selects
  `std::hardware_destructive_interference_size` when available and falls back
  to 64 bytes otherwise.
- Static assertions verify the cell and padded-counter layout assumptions.
  This reduces false-sharing risk but is not a formal performance guarantee.

### 10.2 Enqueue

1. Reject payloads larger than `N` before claiming a position.
2. Load `enqueue_pos_` relaxed.
3. Acquire the selected cell generation.
4. Equal generation means claimable; a smaller pre-wrap generation means full;
   a larger value means refresh the position.
5. Claim with a relaxed weak CAS.
6. Copy payload and logical sequence `position + 1` into the exclusively owned
   cell.
7. Publish by storing `position + 1` to the cell generation with release.

### 10.3 Dequeue

1. Load `dequeue_pos_` relaxed.
2. Acquire the selected cell generation.
3. Generation `position + 1` means claimable; a smaller pre-wrap generation
   means empty; a larger value means refresh the position.
4. Claim with a relaxed weak CAS.
5. Copy the exclusively owned cell into the destination.
6. Release it for reuse with generation `position + capacity` and release
   ordering, regardless of copy status.

The acquire/release cell generation transfers ownership of ordinary payload
bytes. Position atomics allocate unique claims but do not publish payload.

### 10.4 Short output and shutdown

Once dequeue CAS succeeds, rolling the position back would violate queue order.
A short destination therefore returns `message_too_large`, zero bytes, and the
message sequence **and consumes the message**. This policy is tested and differs
from SPSC and SPMC cursor behavior.

MPMC has no close operation. Callers coordinate producer completion externally,
then drain until `try_pop` reports empty. `empty()` is advisory under concurrent
activity.

### 10.5 Progress and lifetime limits

The implementation contains no mutex or condition variable and is called
mutex-free. The project does not make an official lock-free or wait-free claim.
A producer delayed after claiming a position can delay observation of later
positions, and platform atomic guarantees have not been elevated into a
supported progress contract.

Monotonic `std::size_t` position/generation wraparound is unsupported. The
comparison logic assumes the counters stay in the pre-wrap operating domain.

## 11. Behavioral Invariants

- A successful work-sharing push is consumed at most once.
- SPSC and MPMC never overwrite unread cells.
- MPMC cell payload access occurs only between acquire ownership and release
  handoff for the active generation.
- MPMC enqueue and dequeue position CAS operations give unique claims.
- Blocking closure is monotonic and preserves its queued work.
- Multicast retains at most `capacity` newest generations and reports lag.
- Successful fixed-message reads report exact active bytes and stored sequence.

These are implementation/test-supported invariants, not a formal proof.

## 12. Build and Package System

The root CMake project is `BoundedConcurrentQueues` at version `0.1.1`, aligned
with the next public release after the initial `v0.1.0` tag. Its implementation
target is the `orbitqueue` interface library with aliases
`OrbitQueue::orbitqueue` and `BoundedConcurrentQueues::orbitqueue`; it exports
C++20, include paths, warning policy, and optional sanitizer flags.

| CMake option | Default | Effect |
| --- | --- | --- |
| `ORBITQUEUE_BUILD_TESTS` | `ON` | Unit, header, package, and CTest registration |
| `ORBITQUEUE_BUILD_BENCHMARKS` | `ON` | JSON benchmark executable |
| `ORBITQUEUE_BUILD_STRESS` | `ON` | Deterministic-input stress executable |
| `ORBITQUEUE_ENABLE_BOOST_BENCHMARKS` | `OFF` | Optional Boost benchmark scenarios |
| `ORBITQUEUE_ENABLE_WARNINGS` | `ON` | `/W4` or strict GCC/Clang warnings |
| `ORBITQUEUE_ENABLE_SANITIZERS` | `OFF` | Selected sanitizer compile/link flags |
| `ORBITQUEUE_SANITIZERS` | `address,undefined` | Sanitizer list when enabled |

The public header install rule covers the entire `include/` tree, including
`mpmc_queue.h`. CMake package export installs `OrbitQueueConfig.cmake` and a
version file. The isolated downstream CTest installs to a temporary prefix,
uses `find_package(OrbitQueue CONFIG REQUIRED)`, verifies both
`OrbitQueue::orbitqueue` and `BoundedConcurrentQueues::orbitqueue`, includes
SPSC and MPMC headers, and executes one round trip through each queue.

The installed package name, exported target, `include/orbitqueue` path,
`orbitqueue` namespace, `ORBITQUEUE_*` options, and version macros are retained
for source/package compatibility. These are compatibility names, not old
project branding. Broad package renaming remains outside this metadata
milestone.

Boost remains benchmark-only, optional, and default OFF. Missing Boost headers
warn and omit only Boost scenarios.

## 13. Correctness Infrastructure

### 13.1 Contract tests

The dependency-free test executable covers:

- `FixedMessage`: zero/exact/oversized payloads and short copies;
- blocking: capacity, FIFO, full/empty, close/drain, repeated close, waiter
  wakeup;
- SPSC: FIFO, sequence, wraparound, full/empty, zero payload, oversized input,
  retryable short output, and concurrent payload integrity;
- SPMC: independent cursors, post-publication registration, retained history,
  lag recovery, moves, zero payload, and retryable short output;
- MPMC: capacity 0/1/non-power-of-two rejection, exact/oversized payloads,
  FIFO, full/empty, wraparound, consuming short output, and 50,000 checked
  messages with four producers and four consumers;
- benchmark support: default matrices, range union, JSON escaping, and complete
  output schema.

Each public header is also compiled in an isolated translation unit.

### 13.2 MPMC contention validation

The high-contention test encodes producer ID, producer-local sequence, and a
deterministic checksum as bytes. Test-side synchronized structures validate
every payload ID and every queue logical sequence exactly once. The queue
itself contains no test mutex or validation allocation.

### 13.3 Sanitizers

ASan/UBSan and TSan are separate supported build configurations. Sanitizers
inspect only executed schedules and do not prove ordering, no loss, no
duplication, linearizability, progress, or wraparound behavior. Public CI runs
separate ASan/UBSan and TSan Debug CTest jobs with benchmarks disabled.

### 13.4 Bounded model checking

Four TLA+ models check bounded FIFO conservation, SPSC ownership transfer,
multicast registration/lag semantics, and MPMC sequence-cell claims and reuse.
Reduced GenMC harnesses check the SPSC/MPMC atomic protocols and multicast mutex
exclusion under RC11. These artifacts evolve with the implementation and do not
freeze the API.

The models are executable in CI with pinned tool artifacts. Their scope and
non-claims are recorded in `verification/claims.md`; current state counts and
dynamic validation results are recorded in `verification/results.md`.

## 14. Stress Runner

`orbitqueue_stress` accepts seed, duration, iterations, queue, producer count,
consumer count, payload size, capacity, and verbose mode. It prints the full
configuration before running and detailed first-failure reproduction data.

Payloads carry global and local sequences, producer ID, payload size, checksum,
and a deterministic `std::mt19937_64` byte pattern. Work-sharing scenarios
compare published/consumed membership; SPMC permits lag but rejects impossible
or corrupt reads.

MPMC stress uses multiple producers and consumers, full/empty retry counters,
external producer completion, final drain, duplicate detection, payload
validation, and published/consumed membership comparison. MPMC/all capacity
must be a power of two greater than one. Short reliable stress smokes for SPSC,
blocking, SPMC, and MPMC are registered with CTest.

The seed reproduces data and intentional yield decisions, not operating-system
thread scheduling. Stress is evidence over many operations, not exhaustive
schedule exploration.

## 15. Benchmark Harness

`orbitqueue_benchmark` is a dependency-free JSON-lines harness with options for
duration, warmup, trials, capacity, payload size, producers, consumers, and
queue selection. Every warmup and measured trial validates generated payloads;
any validation error makes the process fail.

Default scenarios:

- SPSC: 1 producer / 1 consumer;
- SPMC: 1 producer / 1, 3, and 10 consumers;
- blocking: 1 producer / 1, 3, and 10 consumers;
- MPMC: 1/1, 4/4, and 4/10;
- optional Boost: 1 producer / 1, 3, and 10 consumers.

Output includes queue, trial, capacity, payload size, worker counts, duration,
warmup, publications, aggregate reads, unique IDs, lag, invalid payloads,
full/empty retries, validation errors, both throughput units, build type,
compiler, git commit, UTC timestamp, and semantic notes.

Work-sharing trials require one valid read per publication after drain. SPMC
aggregate reads are multicast observations and are not treated as unique pops.
The harness retains observed IDs during a trial for duplicate/loss detection;
validation therefore adds CPU and memory overhead to measured work.

Metadata does not capture CPU topology, frequency, affinity, operating-system
noise, or the standard library. Raw trials are emitted without in-process
averaging so later analysis does not lose samples. No latency distribution,
confidence interval, or ranking claim is produced.

## 16. Continuous Integration

GitHub Actions builds and runs CTest on Ubuntu in Debug and Release. Because
stress and benchmark smokes are registered with CTest, the default jobs exercise
them along with package consumption. The CI workflow also runs separate
ASan/UBSan and TSan Debug CTest jobs with benchmarks disabled. A separate
verification workflow runs the pinned TLC models, GenMC protocol harnesses, and
synchronization negative controls. CI does not currently include Windows/macOS,
long stress, performance thresholds, coverage, static analysis, or release
packaging.

## 17. Design Boundaries

The public design deliberately excludes raw allocation macros, global namespace
types, caller-managed ring positions, callback writes into queue storage, and
unchecked capacity or payload access. Queue storage and cursor state remain
owned by the queue implementations.

The build and measurement design also excludes machine-specific include paths,
directory-wide compiler settings, comparisons that conflate multicast reads
with work-sharing pops, and shutdown paths that can strand blocking consumers.
These are current project constraints, not compatibility promises.

## 18. Current Claims and Risks

Supported by current implementation and tests:

- invalid capacities and payload bounds are rejected as documented;
- blocking close wakes tested waiters and preserves drainable work;
- SPSC preserves FIFO payload/sequence order without unread overwrite;
- multicast readers advance independently and detect lost retained history;
- MPMC performs mutex-free unique position claims and tested acquire/release
  payload handoff;
- MPMC contention tests and stress detect no loss, duplication, or corruption
  in executed schedules;
- installed packages expose and execute the four public queue headers.

Explicitly not claimed:

- correctness after position or logical sequence exhaustion;
- lock-free/wait-free progress or immunity to stalled claim holders;
- production readiness, formal verification, or stable ABI;
- correctness outside declared SPSC/SPMC ownership rules;
- superior throughput or latency;
- portability beyond tested compiler/platform configurations.

Primary remaining risks:

| Risk | Current mitigation | Remaining work |
| --- | --- | --- |
| MPMC algorithm defect in an unexecuted schedule | 50k contention test, seeded stress, TSan, bounded TLC and GenMC protocol checking | Larger models, implementation refinement, independent review |
| Position/generation wrap | Explicitly unsupported | Define operational limit or wrap-safe comparison model |
| No MPMC close | External completion and drain in tests/tools | Design separately without weakening try-only correctness |
| Short read consumes MPMC work | Explicit result and contract test | Caller must size destinations to `N` when loss is unacceptable |
| Platform atomic differences | No official progress claim | Add supported-platform atomic capability policy |
| Benchmark distortion | Validation fields and semantic notes | Separate measurement/validation modes only with equivalent gates |
| Documentation drift | Focused docs and context snapshot | Add doc/API checks where practical |

## 19. Validation Record

Earlier validation milestones passed Debug, Release, package, ASan/UBSan, TSan,
minimal-option, and benchmark smoke configurations with Apple Clang 17. Public
Ubuntu Debug/Release CI passed for the package baseline. Boost headers were
unavailable locally, so actual optional Boost scenario execution remains
unverified in that environment.

The current MPMC milestone was validated locally with Apple Clang 17:

- Debug configure/build and CTest: 7/7 passed;
- Release configure/build and CTest: 7/7 passed;
- benchmarks-off configure/build and CTest: 6/6 passed;
- tests-off configure/build: passed;
- full seeded stress (`seed=12345`, all queues, 10,000 iterations): passed;
- MPMC portion of that run: 30,000 pushed, 30,000 popped, no membership
  mismatch or validation failure;
- Release benchmark (`250 ms`, one measured trial): all ten SPSC, SPMC,
  blocking, and MPMC scenarios completed with zero invalid payloads and zero
  validation errors;
- ASan/UBSan build and CTest with benchmarks disabled: 6/6 passed;
- TSan build and CTest with benchmarks disabled: 6/6 passed;
- downstream install/`find_package`/runtime test: passed in Debug, Release,
  benchmarks-off, ASan/UBSan, and TSan configurations.

The expanded benchmark run initially exposed an accounting defect that treated
expected duplicate multicast observations as duplicate work-sharing pops. The
accounting was scoped to work-sharing scenarios, the CTest smoke was expanded
to multiple multicast consumers, and the complete matrix then passed.

Passing sanitizer runs mean no issue was reported on executed paths; they are
not proofs. Boost headers were not requested for this focused validation, so
the optional Boost scenario remains outside this milestone's local evidence.

The project identity milestone was validated locally with Apple Clang 17:

- the CMake cache reports `CMAKE_PROJECT_NAME=BoundedConcurrentQueues`;
- Debug configure/build and CTest: 7/7 passed;
- Release configure/build and CTest: 7/7 passed;
- benchmarks-off configure/build and CTest: 6/6 passed;
- tests-off configure/build: passed;
- downstream `find_package(OrbitQueue)` compatibility: passed in all three
  test-enabled configurations;
- short seeded all-queue stress: passed with zero validation failures;
- short Release benchmark: all ten scenarios reported zero invalid payloads
  and zero validation errors;
- `git diff --check` and current-facing identity/path audits: passed.

The bounded verification milestone added the following evidence:

- TLC completed all four finite models with no invariant violations;
- the MPMC TLC model explored 14,265 distinct states across two cell
  generations;
- GenMC reported no error for the SPSC, multicast-mutex, or capacity-two MPMC
  protocol harnesses under RC11;
- fresh Debug CTest passed 7/7;
- fresh Release CTest passed 7/7;
- fresh ASan/UBSan and TSan CTest passed 6/6 each;
- five small-capacity Release stress seeds completed with zero validation
  failures, including 200,000 total MPMC pushes and pops;
- full ASan/UBSan and TSan all-queue stress completed without a report.

## 20. Contributor Workflow

Default validation:

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure
```

Focused MPMC stress:

```sh
./build-debug/stress/orbitqueue_stress \
  --queue mpmc --seed 12345 --duration-ms 250 --iterations 10000 \
  --producers 4 --consumers 4 --payload-size 64 --capacity 1024
```

Benchmark sample:

```sh
./build-release/benchmarks/orbitqueue_benchmark \
  --queue mpmc --duration-ms 250 --warmup-ms 50 --trials 3
```

Before changing synchronization:

1. state the affected contract and linearization point;
2. update the memory-ordering rationale;
3. add a deterministic regression test;
4. pass Debug, Release, package, stress, and benchmark validation;
5. pass ASan/UBSan and TSan where supported;
6. review every stronger progress or safety claim.

## 21. Prioritized Roadmap

### P0: MPMC confidence

- Independently review the sequence-cell protocol and comparison assumptions.
- Add model checking or systematic interleaving exploration.
- Schedule longer multi-seed contention runs under TSan.
- Decide how position exhaustion is prevented or represented.

### P1: Portability and integration

- Add macOS and Windows compiler coverage.
- Validate optional Boost scenarios in a Boost-equipped job.
- Test declared minimum CMake and installed-target warning/sanitizer behavior.

### P2: MPMC API research

- Evaluate a close protocol as a separate design, not an incidental flag.
- Consider runtime diagnostics for invalid ownership on restricted queues.
- Define platform requirements before considering a stronger progress claim.

### P3: Measurement quality

- Capture CPU/OS/topology metadata and retain raw benchmark artifacts.
- Add external statistics and latency distributions.
- Profile before changing synchronization, then preserve semantic equivalence.

## 22. Snapshot Summary

Bounded Concurrent Queues for C++20 has four explicit bounded queue contracts,
including a mutex-free sequence-cell MPMC work-sharing queue. Its engineering
evidence combines narrow APIs, documented memory ownership, deterministic
payload validation, high-contention tests, sanitizer paths, semantically honest
benchmarks, package consumption, and explicit non-claims. Further work should
deepen MPMC verification and portability rather than broaden its API
prematurely.
