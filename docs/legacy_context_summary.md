# Legacy Context Summary

The original OrbitQueue was an educational C++20 prototype exploring
fixed-size in-memory queues. It had no automated tests or CI, hard-coded build
assumptions, conflicting global types, unclear multicast SPMC semantics, and
unsafe APIs that exposed raw writes and caller-managed ring indices. Its
benchmark also compared queues with different delivery semantics and payloads.

OrbitQueue v2 starts from explicit contracts, bounded span-based payload APIs,
portable CMake, correctness tests, sanitizer support, and semantically honest
benchmarks. It is the supported successor and useful replacement for the
prototype, but it does not reuse the legacy queue algorithms.

The original research was inspired by the CppCon 2022 talk
[Trading at Light Speed](https://youtu.be/8uAW5FQtcvE) and asked whether
localized per-slot metadata could reduce contention relative to global queue
indices. V2 preserves that question without preserving the unverified claim
that the legacy protocol was race-free or faster.

Useful benchmark parity has been recreated with safe shutdown, comparable
sequence payloads, explicit metrics, 1/3/10-consumer matrices where contracts
permit, and an optional Boost.Lockfree path. Historical images and the full v1
technical context are preserved under [`docs/legacy`](legacy/README.md) with
clear warnings that they are not current designs or performance evidence.

See [`docs/v1_parity_audit.md`](v1_parity_audit.md) for the disposition of every
legacy feature and the manual checks required before deleting the old
repository.
