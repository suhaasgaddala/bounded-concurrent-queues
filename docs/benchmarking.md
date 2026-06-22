# Benchmarking

`orbitqueue_benchmark` is a dependency-free, contract-aware throughput harness.
It validates every payload while measuring and emits one JSON object per trial.
It is intended for reproducible local comparisons, not publication-quality
performance claims.

## Build and run

Use a Release build for performance work:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
./build-release/benchmarks/orbitqueue_benchmark \
  --duration-ms 1000 --warmup-ms 100 --trials 5
```

Available options are:

```text
--duration-ms <uint64>
--warmup-ms <uint64>
--trials <uint32>
--capacity <uint32>
--payload-size <uint32>
--producers <uint32>
--consumers <default|single|comma-separated counts>
--queue <all|spsc|blocking|spmc|mpmc|boost>
--help
```

Payload size must be between 32 and 256 bytes because every message carries a
32-byte validation header. The general default consumer preset is `1,3,10`.
MPMC uses its `1/1`, `4/4`, `4/10` matrix unless producers or consumers are
explicitly configured. Selecting one queue applies configured values where its
contract permits them. MPMC scenarios require a power-of-two capacity greater
than one.

## Scenario matrix

The default `--queue all` run uses:

| Queue | Producers / consumers | Delivery model |
| --- | --- | --- |
| `spsc` | 1 / 1 | Exclusive work sharing |
| `spmc_multicast` | 1 / 1, 1 / 3, 1 / 10 | Retained-history multicast |
| `blocking_mpmc` | 1 / 1, 1 / 3, 1 / 10 | Exclusive work sharing |
| `mpmc` | 1 / 1, 4 / 4, 4 / 10 | Exclusive work sharing |
| `boost_lockfree_work_sharing` | 1 / 1, 1 / 3, 1 / 10 | Optional exclusive work-sharing baseline |

SPSC is never run with multiple producers or consumers because that would
invalidate its contract. SPMC always has one producer. Passing `--producers`
configures the blocking and MPMC scenarios; passing `--consumers` configures
their consumer matrices. The three-entry MPMC matrix above is used by default
for either `--queue all` or `--queue mpmc`.

## Warmup and trials

Each scenario receives a separate warmup run before measured trials. Warmup
results are discarded, but the executable exits unsuccessfully if warmup
validation fails. `--warmup-ms 0` disables warmup.

Trials create a fresh queue and fresh threads. One JSON line is emitted per
measured trial. The harness deliberately reports raw trials instead of averaging
them in-process so callers can retain samples, choose statistics, and identify
outliers without losing information.

## Output schema

Every JSON object contains:

| Field | Meaning |
| --- | --- |
| `queue`, `trial` | Scenario identity and one-based measured trial number |
| `capacity`, `payload_size` | Queue and message configuration |
| `producer_count`, `consumer_count` | Active worker counts |
| `duration_ms`, `warmup_ms` | Requested measured and warmup intervals |
| `messages_published` | Successful producer operations |
| `aggregate_reads` | Successful reads summed across consumers |
| `unique_sequences_verified` | Distinct validated payload IDs observed |
| `dropped_or_lagged` | SPMC cursor-recovery events; not an exact skipped-message count |
| `invalid_payloads` | Checksum, header, or generated-byte mismatches |
| `full_retries`, `empty_retries` | Non-blocking boundary results retried by workers |
| `validation_errors` | All detected correctness failures, including duplicates and incomplete drains |
| `throughput_messages_per_second` | Publications divided by requested measured seconds |
| `throughput_reads_per_second` | Aggregate reads divided by requested measured seconds |
| `build_type`, `compiler`, `git_commit`, `timestamp` | Build and run provenance, or `unknown` where unavailable |
| `notes` | Delivery-semantics caveat for the scenario |

Work-sharing scenarios require exactly one validated read for every successful
publication after drain. SPMC readers may lag and may each observe the same
publication, so its aggregate reads are neither unique pops nor directly
comparable to work-sharing throughput. Any validation error makes the process
exit nonzero.

Payload validation and ID retention add measurement overhead. The harness stores
observed IDs during each trial to detect duplicates and missing work; memory use
therefore grows with successful reads. Scheduling, CPU frequency, topology,
affinity, background load, allocator behavior, and compiler flags can all affect
results. The metadata is provenance, not full environment capture, and the
harness does not report latency distributions or statistical significance.

## Optional Boost baseline

Enable Boost scenarios with:

```sh
cmake -S . -B build-boost -DCMAKE_BUILD_TYPE=Release \
  -DORBITQUEUE_ENABLE_BOOST_BENCHMARKS=ON
```

Boost is benchmark-only and default OFF. If `boost/lockfree/queue.hpp` is
unavailable, CMake warns and omits only Boost scenarios. The word `lockfree`
identifies the Boost type; its actual progress properties remain
platform-dependent.

The historical chart at
[`docs/legacy/assets/benchmark_v1_historical.png`](legacy/assets/benchmark_v1_historical.png)
is preserved only as a v1 artifact. The legacy benchmark compared different
payloads and delivery semantics and retained insufficient provenance, so its
numbers are not a current-project baseline.
