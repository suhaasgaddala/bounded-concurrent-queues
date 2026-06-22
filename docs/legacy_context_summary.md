# Legacy Prototype Context

The original OrbitQueue/AtomicRing-style repository was an educational C++20
prototype exploring fixed-size in-memory queues. It had no automated tests or
CI, contained hard-coded build assumptions and conflicting global types, used
unsafe raw-write and caller-managed-index APIs, and did not define multicast
delivery precisely. Its benchmark also compared unlike delivery semantics and
payloads.

Bounded Concurrent Queues for C++20 supersedes that prototype with explicit
contracts, bounded span-based payload APIs, portable target-scoped CMake,
correctness tests, deterministic stress, sanitizer support, and semantically
separated benchmark metrics. The current queue algorithms do not reuse the
legacy implementations.

The original research was inspired by the CppCon 2022 talk
[Trading at Light Speed](https://youtu.be/8uAW5FQtcvE) and asked whether
localized per-slot metadata could reduce contention relative to global queue
indices. The current project preserves that research question without
preserving the unverified claim that the old protocol was race-free or faster.

Useful benchmark concepts were recreated with safe shutdown, comparable
sequence-bearing payloads, explicit metrics, contract-valid consumer matrices,
and an optional Boost.Lockfree path. Historical images and the full legacy
technical context remain under [`docs/legacy`](legacy/README.md). They are
historical artifacts, not current designs or performance evidence.

See [`docs/v1_parity_audit.md`](v1_parity_audit.md) for the disposition of
legacy features and the manual checks required before retiring the original
repository.
