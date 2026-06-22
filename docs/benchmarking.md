# Benchmarking

The benchmark measures each delivery contract in a separate scenario. The
default matrix is:

| Queue | Delivery | Consumers |
| --- | --- | --- |
| `spsc` | Work sharing under the one-consumer contract | 1 |
| `spmc_multicast` | Every registered consumer may read each publication | 1, 3, 10 |
| `blocking_mpmc` | Exclusive work sharing | 1, 3, 10 |
| `boost_lockfree_work_sharing` | Exclusive work sharing, optional | 1, 3, 10 |

All scenarios use one producer, capacity 1024, and the same two-`uint64_t`
payload containing a sequence and its bitwise-complement checksum. SPSC is not
run with multiple consumers because doing so would make the measurement invalid
by construction.

Each scenario emits one JSON object containing queue name, capacity, payload
size, producer and consumer counts, requested duration, publications,
aggregate reads, verified unique sequences, lag events, and validation errors.

`messages_published` counts accepted producer operations. `aggregate_reads`
counts all successful consumer reads. `unique_sequences_verified` is the union
of sequences whose payload matched its sequence. In multicast, one publication
can contribute several aggregate reads, so aggregate reads are not comparable
to unique SPSC pops. `dropped_or_lagged` counts detected cursor recovery events,
not the exact number of messages skipped.

`validation_errors` counts payload/sequence mismatches, duplicate or
non-monotonic observations within a consumer, and work-sharing drain-count
mismatches. Any nonzero value makes the benchmark executable exit unsuccessfully
so the CTest smoke run cannot silently accept a detected correctness failure.

Monotonic sequence ranges replace the original per-message sets. This keeps
correctness accounting bounded by the number of observed gaps rather than the
total message count. It remains measurement overhead and is not a zero-cost
throughput harness.

The blocking scenario uses non-blocking producer pushes, then closes the queue
and lets all consumers drain. This makes shutdown explicit and prevents the
legacy benchmark's empty-queue hang. The optional Boost scenario uses a bounded
Boost.Lockfree queue and a producer-done signal before consumers finish
draining.

Enable Boost scenarios with:

```sh
cmake -S . -B build-boost \
  -DORBITQUEUE_ENABLE_BOOST_BENCHMARKS=ON
```

Boost is a benchmark-only header dependency and is off by default. If
`boost/lockfree/queue.hpp` is unavailable, CMake warns and omits only the Boost
scenarios. The word `lockfree` identifies the Boost type; actual lock-free
properties remain platform-dependent.

The legacy benchmark compared different payloads and different delivery
semantics, lacked correctness checks, and did not retain machine or trial
metadata. Its totals therefore were not a fair queue ranking.

The historical chart is preserved at
[`docs/legacy/assets/benchmark_v1_historical.png`](legacy/assets/benchmark_v1_historical.png)
only as a v1 artifact. It is not a v2 result or regression baseline.

The included executable is a small inspectable smoke benchmark, not a full
performance study. It has no warmup, affinity control, latency distribution,
or statistical repetitions. Throughput is not correctness; tests and
sanitizers are separate gates that must pass before performance interpretation.
