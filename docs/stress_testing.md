# Stress Testing

`orbitqueue_stress` is a dependency-free, deterministic-input concurrency
runner. It complements contract tests by exercising many operations under real
thread scheduling while retaining enough seed and configuration data to replay
the same payload stream.

## Build and run

Stress support is enabled by default with `ORBITQUEUE_BUILD_STRESS=ON`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/stress/orbitqueue_stress \
  --queue all \
  --seed 12345 \
  --duration-ms 1000 \
  --iterations 100000 \
  --producers 3 \
  --consumers 3 \
  --payload-size 64 \
  --capacity 127 \
  --verbose
```

The supported options are:

| Option | Meaning |
| --- | --- |
| `--seed <uint64>` | Reproduces payload and deterministic yield decisions |
| `--duration-ms <uint64>` | Maximum producer run time |
| `--iterations <uint64>` | Per-producer maximum for MPMC work sharing; publication maximum for SPSC/SPMC |
| `--queue <all|spsc|blocking|spmc|mpmc>` | Selects scenarios |
| `--producers <uint32>` | Blocking/MPMC producer count; queue-specific contracts still apply |
| `--consumers <uint32>` | Blocking/SPMC/MPMC consumer count |
| `--payload-size <uint32>` | Payload bytes, currently 32 through 256 |
| `--capacity <uint32>` | Runtime queue capacity |
| `--verbose` | Includes verbose mode in the reproduction configuration |

When `--queue all` is used, SPSC always runs with its required one producer and
one consumer, and SPMC always uses one producer. The configured producer and
consumer counts apply where the selected queue contract permits them.

## Validation model

Each payload contains a global sequence, producer-local sequence, producer ID,
declared payload size, checksum seed, and pseudorandom trailing pattern. The
pattern is generated with `std::mt19937_64`. Consumers regenerate the expected
payload and compare every byte, detecting stale, torn, reordered, duplicated,
or corrupted observations.

The runner prints `stress_config` before starting, one `stress_result` per
scenario, and a final `stress_summary`. The first failure includes the queue,
seed, operation, expected/observed sequence and checksum, and reason. Any
validation failure returns a nonzero process status.

Thread schedules are not deterministic. The seed makes generated data and
intentional yield decisions reproducible; reruns still explore scheduler
variation. This is useful evidence, not a proof of all possible interleavings.

## Scenario behavior

- **SPSC:** validates FIFO sequence/result agreement, checksum patterns,
  wraparound, full/empty retries, drain completion, and undersized-output retry.
- **Blocking MPMC baseline:** validates unique producer ID/sequence pairs,
  duplicate and loss detection, close/drain, and blocked producer/consumer
  wakeup smoke cases.
- **SPMC multicast:** validates monotonic per-consumer sequences, complete
  payload patterns, aggregate/unique reads, and contract-allowed lag recovery.
- **MPMC:** validates multiple producers and consumers, queue sequence order,
  full/empty retries, payload uniqueness, duplicate/loss detection, and
  close/drain completion.

Short scenario-specific smoke runs are registered with CTest. Long runs are
intentional local or scheduled validation and are not part of normal CI.
