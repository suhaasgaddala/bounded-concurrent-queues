# OrbitQueue v1 Parity Audit

This audit compares the legacy OrbitQueue repository at commit `efbddb4` with
OrbitQueue v2's `v1-parity-migration` branch. Evidence comes from the archived
[v1 project context](legacy/v1_project_context.md), the current
[v2 project context](../PROJECT_CONTEXT.md), and direct inspection of both
repositories on 2026-06-22.

The objective is useful-value parity, not source or API compatibility. Unsafe,
misleading, machine-specific, and obsolete behavior is classified explicitly
instead of being copied into v2.

## Summary

| Category | Result |
| --- | --- |
| Already present in v2 | Core queue concepts, safer SPSC and multicast replacements, blocking queue, bounded payloads, C++20, tests, CI, sanitizers, portable CMake, packaging, honest JSON metrics |
| Recreated during this task | Optional Boost.Lockfree scenarios, blocking benchmark, 1/3/10 consumer matrices, compact correctness tracking, benchmark smoke gate, historical assets/context, successor identity, design inspiration |
| Intentionally excluded | Unsafe legacy algorithms/APIs, hard-coded paths, mandatory Boost, hanging shutdown, unfair ranking claims, raw historical chart as current evidence |
| Still missing | Irrecoverable raw benchmark provenance and external GitHub/repository metadata that cannot be represented as code behavior |

## Complete Feature Disposition

### Already present in v2

| v1 feature or concept | v2 disposition |
| --- | --- |
| C++20 project | Retained with an explicit C++20 interface requirement. |
| OrbitQueue research identity | Retained as the `OrbitQueue` project and `orbitqueue` library target rather than the legacy `AtomicRing` executable identity. |
| Fixed-size in-memory queues | Retained with template-bounded payload storage. |
| Bounded blocking queue | Rebuilt as `BlockingQueue<T>` with zero-capacity rejection, non-blocking operations, close, wakeup, and drain behavior. |
| Single-producer/single-consumer intent | Rebuilt as `SPSCQueue<N>` with one producer, one consumer, FIFO sequences, bounded spans, and no unread overwrite. |
| Single-producer multicast intent | Rebuilt as `SPMCMulticastQueue<N>` with registered consumers, independent cursors, retained-history lag detection, and no caller-managed index. |
| Ring capacity 1024 in benchmarks | Retained as the common benchmark capacity. |
| Fixed payload benchmark | Normalized to one sequence-plus-checksum payload for every scenario. |
| Producer/consumer thread harness concept | Retained in scenario-specific, contract-aware benchmark functions. |
| Cache-contention research question | Retained as motivation. V2 aligns SPSC ownership counters but makes no portable cache-line or performance guarantee. |
| Yield on non-blocking backpressure | Retained where SPSC, blocking `try_push`, and Boost report temporary unavailability. |
| Count operations during a fixed duration | Retained with a configurable millisecond duration. |
| Recognition that multicast and work sharing differ | Promoted to an explicit benchmark rule and metric documentation. |
| MIT licensing | v2 has its own MIT license; the v1 notice is separately preserved for migrated artifacts. |
| Original authorship/provenance | Preserved in the archived v1 context, original license notice, and parity audit rather than copied into newly written algorithms. |

### Recreated in v2 during this task

| v1 feature or value | Clean v2 recreation |
| --- | --- |
| Boost.Lockfree comparison | Optional `ORBITQUEUE_ENABLE_BOOST_BENCHMARKS=ON` path. Boost remains absent from the core and default build. Missing headers produce a warning and disable only Boost scenarios. |
| Blocking queue benchmark code | Active `blocking_mpmc` scenarios use `try_push`, `close`, and draining `pop` calls so shutdown cannot strand a waiter. |
| One, three, and ten consumers | Multicast, blocking, and optional Boost scenarios use the full matrix. SPSC remains one consumer because multiple consumers violate its contract. |
| Benchmark correctness checks | Payloads carry sequence identities, compact range tracking counts unique sequences, and nonzero validation errors fail the benchmark process. |
| Human-only benchmark totals | Replaced by machine-readable JSON lines with explicit queue and workload metadata. |
| Unequal string/integer benchmark payloads | Replaced by one sequence-plus-checksum payload representation shared by every scenario. |
| Historical benchmark chart | Preserved under `docs/legacy/assets` with a prominent non-reproducibility and semantic-mismatch warning. |
| Global-index SPMC diagram | Preserved as historical context, clearly labeled as not depicting v2. |
| README design motivation | Rewritten to preserve the CppCon inspiration and contention research question without asserting that v1 was correct or superior. |
| Legacy repository technical context | Archived in full under `docs/legacy/v1_project_context.md`. |
| v2 successor identity | README and legacy documentation now state that v2 is the supported successor and useful replacement. |

### Intentionally excluded because unsafe, misleading, or obsolete

| v1 feature or behavior | Reason for exclusion |
| --- | --- |
| `WriteCallback` raw slot writes | Exposed internal memory and could exceed the 64-byte slot. Replaced by bounded input spans. |
| Caller-managed consumer ring indices | Allowed unchecked indexing and could not identify generations. Replaced by move-only consumer cursors. |
| Per-slot odd/even SPMC version protocol | Did not prevent producer overwrite during non-atomic payload copies and allowed lost consumer version updates. |
| SPSC `unread` and version protocol | Subtle, untested, skipped slots on write failure, and lacked bounds validation. Replaced by monotonic head/tail ownership. |
| Conflicting global `Block`, `Header`, and alias names | Prevented safe inclusion of both legacy headers. All v2 APIs are namespaced and queue internals are private. |
| Unchecked zero capacity, payload size, and indices | Produced undefined behavior. V2 validates capacity and bounded spans. |
| Mandatory Boost dependency | Made the core and only executable dependent on an optional comparison library. |
| Hard-coded Homebrew Boost/LLVM paths | Non-portable and set compilers too late. V2 uses target-scoped portable CMake discovery. |
| Directory-wide CMake configuration | Leaked settings across targets. V2 uses target-scoped requirements and an installable interface library. |
| `AtomicRing` monolithic executable | Mixed product code and measurement code. V2 separates installed headers, tests, and benchmark targets. |
| Type-erased `std::function` benchmark harness | Hid queue-specific stop and drain behavior. V2 uses explicit scenario drivers that encode each contract. |
| Blocking benchmark's unconditional `pop()` shutdown | Could hang forever after the stop flag. V2 closes and drains explicitly. |
| `std::atomic<int>` aggregate count | Could overflow and conflated unlike work. V2 uses 64-bit, named metrics. |
| Five-second fixed duration | Needlessly slows smoke feedback. V2 accepts a positive millisecond duration. |
| Claim that per-slot versioning was significantly faster | Unsupported by comparable work or correctness evidence. |
| Historical chart as a regression baseline | Raw data, environment, and generation process are absent, and its bars count different delivery semantics. |
| Empty legacy `.gitignore` | Offered no value and risked tracking generated files. V2 ignores build and local artifacts. |
| Source/API compatibility | The legacy API is unsafe and was never stable; compatibility would preserve defects rather than useful behavior. |

### Still missing and why

| Missing item | Reason and deletion impact |
| --- | --- |
| Original raw benchmark observations | They were never committed and cannot be reconstructed from the PNG. The labeled image is the only recoverable artifact. |
| Original chart-generation script | It did not exist in the tracked v1 repository. Future v2 charts must use new tracked raw data and scripts. |
| Original benchmark machine/compiler metadata | It was not recorded. No performance provenance can be recovered. |
| Exact v1 source implementation in v2 | Deliberately not migrated because it is unsafe and obsolete. The archived context documents its behavior and risks. |
| Full v1 Git object history | Not embedded into v2 because a source-history bundle would retain obsolete implementation rather than product functionality. Archive v1 separately before deletion if legal or historical provenance beyond the documented commit list is required. |
| GitHub issues, pull requests, stars, forks, redirects, and repository settings | These are external service state, not repository files. They must be checked manually before deleting the GitHub repository. |

## SPSC Intent Confirmation

The useful v1 SPSC intent was exactly-once work sharing without overwriting an
unread slot. V2 covers that intent more directly:

- the queue contract permits exactly one producer and one consumer;
- full capacity returns `full` rather than overwriting or skipping a slot;
- FIFO sequence numbers start at one;
- boundary tests cover empty, full, FIFO, oversized payloads, and zero capacity;
- a 50,000-message concurrent test validates payload and sequence integrity
  across repeated ring wraparound;
- ASan/UBSan and TSan configurations exercise the test suite.

No legacy SPSC mechanism needs to be retained.

## Replacement Decision

OrbitQueue v2 contains the useful code, concepts, scenarios, assets, and
documentation value identified in the tracked v1 repository. The remaining
missing items are either irrecoverable provenance or external GitHub state.

Before deleting v1, manually verify:

1. whether its Git history needs an offline archival bundle;
2. whether GitHub issues, pull requests, stars, forks, or repository redirects
   need preservation;
3. whether any external links should be redirected to the v2 repository;
4. that the v2 parity commit and validation results are pushed and CI passes.

Do not use deletion itself as the archive mechanism.
