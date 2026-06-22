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
consumer. Capacity must be nonzero. The initial implementation uses a mutex
around a preallocated `FixedMessage` ring and makes no lock-free or wait-free
claim.

`try_push` returns `full` without overwriting unread data, rejects payloads
larger than the template maximum, and returns `closed` after shutdown.
`try_pop` returns `empty` while an open queue has no data. An undersized output
returns `message_too_large` without consuming the front message, allowing a
retry with a larger span.

`close` is idempotent, rejects later pushes, and preserves queued messages for
draining. Once the queue is both closed and empty, `try_pop` returns `closed`.
There are no blocking MPMC operations in the current API. Sequence exhaustion
is not handled; applications must not approach `uint64_t` wraparound.

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
the oldest retained sequence, and can resume on its next read. No raw ring
index is exposed. Consumer handles are move-only and must not outlive their
queue. Ordering is increasing publication sequence per consumer.

A mutex serializes publication and payload copies. Consequently this version
makes no lock-free or wait-free claim. There is no shutdown operation because
the API is non-blocking.
