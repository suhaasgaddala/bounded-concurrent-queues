# Legacy OrbitQueue Parity Audit

This historical audit compares the original OrbitQueue repository at commit
`efbddb4` with the successor work completed on the `v1-parity-migration`
branch. Evidence comes from the archived
[legacy project context](legacy/v1_project_context.md), the current
[project context](../PROJECT_CONTEXT.md), and direct inspection of both
repositories on 2026-06-22.

The objective was useful-value parity, not source or API compatibility. Unsafe,
misleading, machine-specific, and obsolete behavior was classified explicitly
instead of being copied into the current library.

## Summary

| Category | Result |
| --- | --- |
| Present in the successor | Core queue concepts, safer SPSC and multicast replacements, blocking queue, bounded payloads, C++20, tests, CI, sanitizers, portable CMake, packaging, and validated JSON metrics |
| Recreated during parity work | Optional Boost.Lockfree scenarios, blocking benchmark, consumer matrices, correctness tracking, benchmark smoke gate, historical assets/context, and design provenance |
| Intentionally excluded | Unsafe legacy algorithms/APIs, hard-coded paths, mandatory Boost, hanging shutdown, unfair ranking claims, and historical charts as current evidence |
| Still unavailable | Irrecoverable raw benchmark provenance and external GitHub state that cannot be represented as code behavior |

## Complete Feature Disposition

### Present in the successor

| Legacy feature or concept | Current disposition |
| --- | --- |
| C++20 project | Retained with an explicit C++20 interface requirement. |
| Queue research identity | Retained under Bounded Concurrent Queues for C++20 while preserving the `orbitqueue` source/package API for compatibility. |
| Fixed-size in-memory queues | Retained with template-bounded payload storage. |
| Bounded blocking queue | Rebuilt as `BlockingQueue<T>` with capacity validation, try operations, close, wakeup, and drain behavior. |
| Single-producer/single-consumer intent | Rebuilt as `SPSCQueue<N>` with FIFO sequences, bounded spans, and no unread overwrite. |
| Single-producer multicast intent | Rebuilt as `SPMCMulticastQueue<N>` with registered consumers, independent cursors, retained-history lag detection, and no caller-managed index. |
| Multiple-producer/multiple-consumer work sharing | Implemented as a bounded mutex-free `MPMCQueue<N>` with sequence-numbered cells and try-only operations. |
| Common benchmark capacity | Retained as a configurable capacity with 1024 as the default. |
| Fixed benchmark payload | Normalized to one sequence-bearing, checksummed payload generator for every scenario. |
| Producer/consumer harness | Retained in scenario-specific drivers that preserve each queue contract. |
| Cache-contention research question | Retained as motivation without a portable cache-line or performance guarantee. |
| Backpressure retry | Retained where try operations report temporary `full` or `empty` states. |
| Fixed-duration measurement | Retained with configurable duration, warmup, and trials. |
| Multicast/work-sharing distinction | Promoted to an explicit benchmark rule and metric definition. |
| MIT licensing | The current project has its own MIT license; the original notice is preserved with migrated artifacts. |
| Authorship and provenance | Preserved in the archived context, original license notice, and this audit rather than copied into new algorithms. |

### Recreated during parity work

| Legacy value | Clean recreation |
| --- | --- |
| Boost.Lockfree comparison | Optional `ORBITQUEUE_ENABLE_BOOST_BENCHMARKS=ON` path; Boost remains absent from the core and default build. |
| Blocking benchmark | `blocking_mpmc` scenarios use explicit close and drain behavior so shutdown cannot strand a waiter. |
| Consumer matrices | Multicast, blocking, and optional Boost scenarios use contract-valid consumer counts; SPSC remains one consumer. |
| Benchmark correctness checks | Generated payloads, unique-ID accounting, retry counters, and nonzero validation failure exits. |
| Human-only totals | Replaced by JSON lines with workload, validation, throughput, and provenance fields. |
| Unequal benchmark payloads | Replaced by one shared payload representation. |
| Historical benchmark chart | Preserved under `docs/legacy/assets` with reproducibility and semantic warnings. |
| Global-index SPMC diagram | Preserved as historical context, clearly labeled as not depicting the current implementation. |
| Design motivation | Preserved without asserting that the original algorithm was correct or superior. |
| Legacy technical context | Archived under `docs/legacy/v1_project_context.md`. |

### Intentionally excluded

| Legacy behavior | Reason for exclusion |
| --- | --- |
| `WriteCallback` raw slot writes | Exposed internal memory and could exceed the slot. Replaced by bounded input spans. |
| Caller-managed consumer indices | Allowed unchecked indexing and could not identify generations. Replaced by owned consumer cursors. |
| Per-slot odd/even SPMC protocol | Did not prevent producer rewrite during a non-atomic payload copy and allowed lost consumer metadata updates. |
| SPSC `unread` and version protocol | Subtle, untested, skipped slots on failure, and lacked bounds validation. Replaced by head/tail ownership. |
| Conflicting global types and aliases | Prevented safe inclusion. Current APIs are namespaced and internals are private. |
| Unchecked capacity, payload size, and indices | Produced undefined behavior. Current constructors and span APIs validate boundaries. |
| Mandatory Boost | Made the core depend on an optional comparison library. |
| Hard-coded Homebrew/LLVM paths | Non-portable. Current CMake discovery is target-scoped. |
| Directory-wide CMake configuration | Leaked settings across targets. Current requirements attach to explicit targets. |
| `AtomicRing` monolithic executable | Mixed library and measurement code. Current headers, tests, stress, and benchmarks are separate. |
| Type-erased benchmark callbacks | Hid queue-specific stop and drain behavior. Current drivers encode each contract explicitly. |
| Unconditional blocking-pop shutdown | Could hang after stop. The replacement closes and drains explicitly. |
| Narrow aggregate counter | Could overflow and conflated unlike work. Current metrics are 64-bit and named. |
| Fixed five-second duration | Needlessly slowed smoke feedback. Duration is configurable. |
| Per-slot versioning performance claim | Unsupported by equivalent work or correctness evidence. |
| Historical chart as regression baseline | Raw data, environment, and generation process are absent; bars count different delivery semantics. |
| Source/API compatibility | The old API was unsafe and never stable; preserving it would preserve defects. |

### Still unavailable

| Missing item | Reason and retirement impact |
| --- | --- |
| Original raw benchmark observations | Never committed and cannot be reconstructed from the image. |
| Original chart-generation script | Did not exist in the tracked repository. Future charts require new tracked data and scripts. |
| Original machine/compiler metadata | Was not recorded; no performance provenance can be recovered. |
| Exact legacy implementation in the current tree | Deliberately excluded as unsafe and obsolete; archived context documents its behavior. |
| Full legacy Git object history | Must be archived separately if provenance beyond this audit is required. |
| GitHub issues, pull requests, stars, forks, redirects, and settings | External service state requiring manual review before retirement. |

## SPSC Intent Confirmation

The useful legacy SPSC intent was exactly-once work sharing without overwriting
an unread slot. The current queue covers that intent directly:

- exactly one producer and one consumer are contractual;
- full capacity returns `full` without overwrite or skipped publication;
- FIFO logical sequences start at one;
- boundary tests cover empty, full, FIFO, payload limits, and invalid capacity;
- concurrent tests validate payload and sequence integrity across wraparound;
- ASan/UBSan and TSan configurations exercise the suite.

No legacy SPSC mechanism needs to be retained.

## Replacement Decision

Bounded Concurrent Queues for C++20 contains the useful code, concepts,
scenarios, assets, and documentation identified in the tracked original
repository. Remaining gaps are irrecoverable provenance or external GitHub
state.

Before retiring the original repository, manually verify:

1. whether its Git history needs an offline archival bundle;
2. whether GitHub issues, pull requests, stars, forks, or redirects need
   preservation;
3. whether external links should point to the final successor URL;
4. that current validation results are pushed and CI passes.

Do not use deletion itself as the archive mechanism.
