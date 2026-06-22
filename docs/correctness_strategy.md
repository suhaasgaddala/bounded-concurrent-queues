# Correctness Strategy

The project treats correctness as layered evidence. No single test, stress run,
sanitizer, benchmark, or memory-ordering explanation proves the whole library.

## Validation layers

### Contract tests

Unit-style contract tests cover deterministic state transitions and boundary
behavior: invalid capacity, payload limits, FIFO order, full/empty results,
queue-specific undersized-output behavior, close/drain where supported,
independent multicast cursors, and lag recovery. They provide fast, precise
failures but cover a limited set of thread schedules.

### Header and package tests

Each public header is compiled in an isolated translation unit to catch missing
includes. The downstream package test installs to an isolated prefix, discovers
the compatibility package with `find_package(OrbitQueue)`, builds against
`OrbitQueue::orbitqueue`, and runs a consumer. These checks protect integration
rather than concurrency semantics.

### Deterministic-input stress tests

`orbitqueue_stress` generates sequence-bearing payloads from a printed seed and
`std::mt19937_64`. Each payload contains:

- a global sequence;
- a producer-local sequence;
- a producer ID;
- the declared payload size;
- a checksum seed;
- a reproducible trailing byte pattern.

Consumers regenerate and compare the entire payload. Work-sharing scenarios
track published and consumed membership to detect duplicates and loss.
Multicast scenarios permit contract-defined lag but reject corruption,
impossible sequences, and non-increasing reads.

The same seed reproduces data and intentional yield decisions. Operating-system
thread scheduling remains nondeterministic, so repeated runs with one seed can
still explore different interleavings.

### Sanitizers

- ASan detects many invalid memory accesses and lifetime errors.
- UBSan detects many executed undefined behaviors.
- TSan detects many data races in executed schedules.

Sanitizers do not prove logical ordering, no loss, no duplication, or universal
race freedom. TSan should be run separately from ASan.

### Benchmarks

Benchmarks validate their payloads and fail on detected corruption, but their
purpose is measurement. Benchmark completion is not correctness evidence.
Warmup, trials, environment metadata, and semantic notes improve
reproducibility; they do not replace the gates above.

## Reproducing a stress failure

Copy the complete `stress_config` line or the seed and arguments from the
failure. Rebuild with the same compiler mode, then rerun, for example:

```sh
./build/stress/orbitqueue_stress \
  --queue blocking \
  --seed 12345 \
  --duration-ms 1000 \
  --iterations 100000 \
  --producers 3 \
  --consumers 10 \
  --payload-size 64 \
  --capacity 31 \
  --verbose
```

Then repeat under ASan/UBSan and TSan builds. Preserve the first
`stress_failure` line, compiler identity, build type, operating system, commit,
and whether the failure reproduces. A failure that does not reproduce remains
a defect signal; it is not permission to discard the report.

## Required gate for synchronization changes

Before merging a synchronization change:

1. State the queue contract and linearization points affected.
2. Update `docs/memory_model.md` with the happens-before argument.
3. Add a deterministic test that would fail under the incorrect behavior.
4. Pass Debug and Release builds and CTest.
5. Pass short deterministic stress scenarios.
6. Pass longer seeded stress runs appropriate to the blast radius.
7. Pass ASan/UBSan and TSan where supported.
8. Pass downstream package consumption.
9. Build benchmarks and verify all validation counters remain zero.
10. Review documentation for stronger claims than the evidence supports.

Performance improvement never overrides a failed correctness gate.

## Interpreting evidence

- A unit-test failure usually identifies a contract regression.
- A stress failure identifies a reproducible payload stream and a schedule-
  sensitive defect candidate.
- A sanitizer report identifies executed undefined or racy behavior.
- A benchmark validation error identifies corrupt or inconsistent measured work.
- A throughput change without repeated comparable trials is an observation, not
  a conclusion.

The strongest practical confidence comes from agreement across all layers while
remaining explicit about schedules and platforms that were not tested.
