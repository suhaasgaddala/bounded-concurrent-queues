# Mutex-Free Bounded MPMC Queue

`MPMCQueue<MaxPayloadSize>` is a bounded, try-only, multi-producer,
multi-consumer work-sharing queue. It adapts Dmitry Vyukov's sequence-numbered
array design to the library's `FixedMessage` and result APIs.

The implementation contains no mutex or condition variable. It is described as
**mutex-free**, not lock-free or wait-free. A progress claim would also need
to account for platform atomic guarantees, preemption after a position claim,
counter exhaustion, and every supported operation. That proof is not part of
this milestone.

## Contract

- Capacity is fixed at construction and must be a power of two greater than
  one. Zero, one, and non-power-of-two values throw `std::invalid_argument`.
- Multiple producers may call `try_push` concurrently.
- Multiple consumers may call `try_pop` concurrently.
- Each successful push is returned by at most one successful pop.
- Unread messages are never overwritten.
- `try_push` returns `full` rather than waiting when no cell is available.
- `try_pop` returns `empty` rather than waiting when no cell is published.
- Payloads larger than `MaxPayloadSize` return `message_too_large` before a
  position is claimed.
- There is no close, blocking, timed-wait, cancellation, or reopen operation.

Logical message sequences start at one. A successful push returns its claimed
position plus one, and the corresponding pop returns the same value stored in
the cell's `FixedMessage`.

## Cell protocol

The queue allocates its cell array once during construction. Every cell holds:

- an atomic generation sequence;
- one `FixedMessage<MaxPayloadSize>`.

The ring uses `position & (capacity - 1)`, which is why capacity must be a power
of two. Cells begin with sequence values equal to their indices.

A producer loads the enqueue position relaxed, inspects the selected cell's
sequence with acquire ordering, and claims the position with a relaxed CAS.
After copying the complete payload into the exclusively claimed cell, it
publishes the cell by storing `position + 1` with release ordering.

A consumer follows the corresponding dequeue protocol. It loads the dequeue
position relaxed, acquires the cell generation, and claims the position with a
relaxed CAS. After copying the payload, it releases the cell for its next ring
generation by storing `position + capacity` with release ordering.

The acquire/release cell sequence is the ownership handoff for ordinary,
non-atomic payload bytes. Position atomics allocate claims; they do not publish
payload contents. There is no dynamic allocation in `try_push` or `try_pop`.
The enqueue and dequeue positions and each cell are 64-byte aligned as a
best-effort false-sharing reduction, not as a portable cache-line guarantee.

## Short destinations

A consumer owns a cell once its dequeue CAS succeeds. Restoring that position
would permit later consumers to pass or duplicate the claimed message, so the
queue does not attempt rollback.

If the destination is shorter than the message, `try_pop` returns:

- status `message_too_large`;
- `bytes_read == 0`;
- the claimed message's logical sequence.

The message is consumed and the cell is released. This differs intentionally
from SPSC and multicast reads, where the single consumer cursor can remain in
place for a larger-buffer retry.

## Linearization and limits

The publication release store makes an enqueued message available. The
successful dequeue position CAS uniquely assigns an available message to one
consumer; the later cell release makes that storage reusable.

`empty()` is an observation of the current dequeue cell and can become stale
immediately under concurrency. It is not a reservation or a termination
protocol. Callers that need shutdown must coordinate producer completion
outside the queue and drain until `try_pop` reports `empty`.

Position and generation exhaustion is unsupported. Correct operation assumes
the monotonic `std::size_t` counters do not wrap. The queue is experimental and
is not claimed production-ready, formally verified, lock-free, or wait-free.
