# Queue Contracts

## BlockingQueue

`BlockingQueue<T>` is a bounded FIFO supporting multiple producers and
consumers through a mutex and condition variables. Capacity must be nonzero.
`push` waits for capacity or closure, while `pop` waits for an item or closure.
`try_push` reports `full` without waiting and `try_pop` reports `empty` without
waiting. `close` wakes all waiters, rejects future pushes, and preserves queued
items for draining. A blocking `pop` returns `std::nullopt` once a closed queue
is empty.

## MPMCQueue

`MPMCQueue<MaxPayloadSize>` is a bounded multi-producer, multi-consumer
work-sharing queue. Every successful push receives an increasing publication
sequence, and each queued message can be popped successfully by at most one
consumer. Capacity must be a power of two greater than one. The implementation
uses a preallocated array of sequence-numbered cells and contains no mutex.

`try_push` returns `full` without overwriting unread data, rejects payloads
larger than the template maximum, and provides no blocking wait operation.
`try_pop` returns `empty` when no cell is published. A consumer claims its
dequeue position before copying, so an undersized output returns
`message_too_large` with zero bytes and the message sequence **and consumes the
message**.

There are no close or blocking operations in the current API. Callers
coordinate producer completion externally and drain with `try_pop`. Position
and generation exhaustion is not handled. The queue is described as
mutex-free, not production-ready, formally verified, lock-free, or wait-free.
See [mpmc_queue.md](mpmc_queue.md) for the cell protocol and memory-ordering
rationale.

## SPSCQueue

`SPSCQueue<MaxPayloadSize>` supports exactly one producer and one consumer. It
is a work-sharing FIFO: each successful publication is popped once. Capacity
must be nonzero. It never overwrites unread messages and reports `full` when
all slots are occupied. Payloads larger than the template limit are rejected.
Sequences start at one and increase in FIFO order. There is no close or
blocking operation in this initial API.

Calls from additional producers or consumers violate the contract. Sequence
counter exhaustion is not handled; practical applications must not approach
`uint64_t` wraparound.

## SPMCMulticastQueue

`SPMCMulticastQueue<MaxPayloadSize>` supports one producer and multiple
registered consumers. It is multicast, not a work-sharing SPMC queue: every
consumer registered before a publication may read that publication
independently. New consumers start at the next publication and do not replay
history.

Publication always advances the sequence and may overwrite the oldest slot
when capacity is exceeded. A slow consumer reports `consumer_lagged`, moves to
the oldest retained sequence, and can continue on its next read. No raw ring
index is exposed. Consumer handles are move-only and must not outlive their
queue. Ordering is increasing publication sequence per consumer.

A mutex serializes publication and payload copies. Consequently this version
makes no lock-free or wait-free claim. There is no shutdown operation because
the API is non-blocking.
