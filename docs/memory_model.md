# Memory Model and Synchronization

The library uses different synchronization strategies for different delivery
contracts. This document explains the current happens-before arguments and the
limits of those arguments. It is engineering rationale, not a formal proof.

## Delivery semantics come first

- **Work sharing** assigns each successful publication to at most one consumer.
  `BlockingQueue`, `SPSCQueue`, and the bounded `MPMCQueue` use this model.
- **Multicast** gives each registered consumer an independent cursor. A single
  `SPMCMulticastQueue` publication may be read by many consumers.

These models require different ownership and retention rules. Aggregate
multicast reads cannot be interpreted as exclusive work-sharing throughput.

## SPSCQueue

`SPSCQueue` supports exactly one producer and one consumer. Its non-atomic
`FixedMessage` slots are safe only because slot ownership is transferred by the
atomic head and tail counters.

### Producer publication

1. The producer loads its own `head_` with `memory_order_relaxed`. No other
   thread writes `head_`, so this load does not need synchronization.
2. It loads consumer-owned `tail_` with `memory_order_acquire` to observe
   released capacity.
3. If capacity exists, it writes payload, size, and sequence into the selected
   slot using ordinary non-atomic operations.
4. It stores the new head with `memory_order_release`.

The consumer's acquire load of that head synchronizes with the release store.
All slot writes sequenced before publication therefore happen before the
consumer copies the slot.

### Consumer release

1. The consumer loads its own `tail_` with `memory_order_relaxed`. No other
   thread writes `tail_`.
2. It loads producer-owned `head_` with `memory_order_acquire` before reading a
   published slot.
3. It copies the slot using ordinary non-atomic operations.
4. Only after a successful copy does it store the new tail with
   `memory_order_release`.

The producer's acquire load of tail observes that release before reusing the
slot. This prevents producer overwrite while the consumer is still copying.

An undersized destination does not release the slot: `tail_` is unchanged, so
the consumer can retry safely with a larger span.

### Observer methods

`empty()` and `full()` use acquire loads, but they are observations rather than
reservations. Concurrent producer or consumer progress can make their result
stale immediately. Correct callers must still handle `empty` and `full` from
the operation itself.

The entire argument depends on one producer and one consumer. Additional
writers can race on slots; additional readers can race on consumption and
payload copies. Such use violates the contract.

## FixedMessage

`FixedMessage` deliberately uses ordinary storage rather than atomics for every
byte. Synchronization belongs to its owner:

- SPSC transfers slot ownership through head/tail release-acquire edges.
- SPMC holds one mutex across assignment and copy.
- MPMC transfers exclusive cell ownership through per-cell generation
  sequences and CAS position claims.

`FixedMessage` alone is not thread-safe. Its bounded span checks prevent buffer
overflow but do not create synchronization.

## BlockingQueue

`BlockingQueue` protects queue storage and closure state with one mutex.
Condition-variable waits release that mutex while blocked and reacquire it
before evaluating their predicate. `close()` changes closure state while
holding the mutex, then notifies all producer and consumer waiters.

Mutex unlock/lock operations provide the required synchronization for queue
items and closure. Condition-variable notification is a wakeup mechanism; the
predicate under the mutex is what makes spurious wakeups safe.

## SPMCMulticastQueue

The multicast queue uses one mutex for registration, publication, sequence
inspection, lag recovery, and payload copies. This is intentionally
conservative.

Per-slot sequence metadata alone cannot prevent a producer from rewriting
ordinary payload bytes while a consumer copies them. Holding the mutex across
both operations excludes that race. It also means publication and every read
are serialized and the implementation makes no lock-free or wait-free claim.

Consumer cursors are owned by their handles. Concurrent calls on one consumer
handle are unsupported. Handles contain a non-owning queue pointer and must not
outlive the queue.

## Bounded MPMCQueue

The bounded `MPMCQueue` follows a sequence-numbered cell protocol. Enqueue and
dequeue position atomics are loaded and claimed with relaxed ordering; these
counters allocate unique positions but do not publish payload bytes. A producer
acquires a cell generation, claims its position, writes the `FixedMessage`, and
publishes `position + 1` with release ordering. A consumer's acquire load of
that value observes the completed payload. After its dequeue CAS and copy, the
consumer stores `position + capacity` with release ordering so a future
producer's acquire load can safely reuse the cell.

The queue contains no mutex and allocates no memory during try operations. It
also has no close operation. An undersized destination consumes the claimed
message and releases the cell because the dequeue position cannot safely be
rolled back. Full details are in [mpmc_queue.md](mpmc_queue.md).

## Why no mutex is not the same as lock-free

"Contains no mutex" describes an implementation detail. Lock-free is a
progress guarantee: system-wide progress must continue even if individual
threads are delayed. Establishing it requires an algorithm-level argument that
covers retry loops, allocation, atomic lock-freedom on supported platforms,
memory reclamation, counter wrap, and every operation's linearization point.

The project does not currently make that progress argument for MPMC or
multicast. The MPMC implementation is therefore described as mutex-free, not
as officially lock-free.

## Why the v1 SPMC algorithm was not reused

The legacy algorithm published per-slot odd/even versions but allowed a later
producer rewrite to overlap a consumer's non-atomic payload copy. A version
check before the copy did not protect the copy itself. Multiple consumers also
stored derived versions and could overwrite one another's metadata updates.
Acquire/release ordering publishes prior writes; it does not prevent a future
writer from racing with an in-progress reader.

The current library keeps the multicast research question but uses a mutex
until an alternative
has an equivalent correctness argument and validation suite.

## Sanitizer evidence and limits

ThreadSanitizer can detect many data races that occur in an executed schedule.
AddressSanitizer and UndefinedBehaviorSanitizer can detect many memory and
undefined-behavior defects on executed paths. None of them:

- explore every interleaving;
- prove linearizability or progress;
- prove absence of logical loss or duplication;
- establish correctness near untested sequence exhaustion;
- validate unsupported producer/consumer counts.

Sanitizers complement contracts, deterministic tests, sequence validation, and
stress runs. They do not replace them.

## Current claims and non-claims

Within documented ownership rules, current tests and stress runs support the
stated FIFO, no-overwrite, close/drain, work-sharing, multicast, and lag
contracts. The project does not claim formal verification, stable ABI,
production readiness, wait-freedom, lock-freedom, or correctness after
`uint64_t` sequence exhaustion.

Before any lock-free MPMC or multicast claim, the project would require:

1. explicit linearization points and progress arguments;
2. a complete memory-ordering rationale for every atomic access;
3. safe slot lifetime and reclamation rules;
4. defined counter-wrap behavior;
5. deterministic contract tests and long seeded stress runs;
6. TSan/ASan/UBSan coverage on supported platforms;
7. architecture-specific atomic capability checks;
8. independent review of the algorithm and its claims.
