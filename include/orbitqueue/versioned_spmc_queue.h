#pragma once

// Per-cell atomic-versioned SPMC queue.
//
// Synchronization is localized to each ring cell instead of a global mutex or a
// pair of shared indices: every cell carries its own even/odd atomic version
// (a seqlock), where even means "stable / readable" and odd means "publish in
// progress". Consumers re-validate the version after copying, so a torn read is
// detected and reported instead of returned. The result is a mutex-free
// single-producer/multiple-consumer multicast queue whose observable contract
// matches the conservative SPMCMulticastQueue, but whose synchronization is
// per-cell atomic versioning rather than a global mutex.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>

#include "orbitqueue/detail/cache_layout.h"
#include "orbitqueue/result.h"

namespace orbitqueue {

// Single producer, multiple consumers, multicast (broadcast) delivery.
//
// Each registered consumer owns an independent cursor and observes every
// publication it is caught up to. Reading does not consume a publication for
// other consumers. A consumer that falls more than `capacity` publications
// behind loses overwritten history and is told (via `consumer_lagged` or
// `overwritten`) to resume from the oldest retained sequence.
//
// Progress / synchronization:
//   * No std::mutex and no std::condition_variable are used.
//   * `try_publish` is wait-free: the single producer never spins or blocks.
//   * `try_read` is non-blocking and completes in a bounded number of steps. If
//     the producer is overwriting the requested cell faster than a consumer can
//     copy it, `try_read` returns `overwritten` instead of spinning forever.
//   * The library does not make a blanket lock-free claim for this queue; it is
//     documented as the atomic-versioned / mutex-free SPMC path.
template <std::size_t MaxPayloadSize>
class VersionedSPMCQueue {
public:
    class Consumer {
    public:
        Consumer(const Consumer&) = delete;
        Consumer& operator=(const Consumer&) = delete;

        Consumer(Consumer&& other) noexcept
            : queue_(std::exchange(other.queue_, nullptr)),
              next_sequence_(other.next_sequence_) {}

        Consumer& operator=(Consumer&& other) noexcept {
            if (this != &other) {
                queue_ = std::exchange(other.queue_, nullptr);
                next_sequence_ = other.next_sequence_;
            }
            return *this;
        }

        [[nodiscard]] ReadResult try_read(
            const std::span<std::byte> destination) noexcept {
            if (queue_ == nullptr) {
                return {QueueStatus::invalid_consumer, 0, next_sequence_};
            }
            return queue_->read(*this, destination);
        }

        // Alias matching the user-facing reader sketch (reader.try_pop(out)).
        [[nodiscard]] ReadResult try_pop(
            const std::span<std::byte> destination) noexcept {
            return try_read(destination);
        }

        [[nodiscard]] std::uint64_t next_sequence() const noexcept {
            return next_sequence_;
        }

    private:
        friend class VersionedSPMCQueue;
        Consumer(VersionedSPMCQueue& queue, const std::uint64_t next_sequence) noexcept
            : queue_(&queue), next_sequence_(next_sequence) {}

        VersionedSPMCQueue* queue_;
        std::uint64_t next_sequence_;
    };

    explicit VersionedSPMCQueue(const std::size_t capacity)
        : capacity_(validate_capacity(capacity)),
          cells_(std::make_unique<Cell[]>(capacity_)) {}

    VersionedSPMCQueue(const VersionedSPMCQueue&) = delete;
    VersionedSPMCQueue& operator=(const VersionedSPMCQueue&) = delete;

    // Registers a consumer that starts at the next future publication. Prior
    // history is not replayed, matching SPMCMulticastQueue semantics.
    [[nodiscard]] Consumer make_consumer() noexcept {
        return Consumer(*this, published_.value.load(std::memory_order_acquire) + 1);
    }

    // Alias kept symmetric with SPMCMulticastQueue::register_consumer.
    [[nodiscard]] Consumer register_consumer() noexcept { return make_consumer(); }

    // Publishes a payload. Single-producer only. Wait-free.
    [[nodiscard]] WriteResult try_publish(
        const std::span<const std::byte> payload) noexcept {
        if (payload.size() > MaxPayloadSize) {
            return {QueueStatus::message_too_large, 0};
        }

        const auto sequence = produced_ + 1;
        Cell& cell = cells_[(sequence - 1) % capacity_];

        // Seqlock write: bump the version to odd (write in progress), publish the
        // payload bytes, then bump to the next even value (stable / readable).
        // Every guarded field is a relaxed atomic so a consumer that overlaps the
        // write performs only well-defined atomic accesses (no data race / UB);
        // the release/acquire fence pair carries the real ordering on weak-memory
        // hardware, while the version re-check discards any torn read.
        const auto version = cell.version.load(std::memory_order_relaxed);
        cell.version.store(version + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);

        cell.sequence.store(sequence, std::memory_order_relaxed);
        cell.size.store(payload.size(), std::memory_order_relaxed);
        for (std::size_t index = 0; index < payload.size(); ++index) {
            cell.storage[index].store(payload[index], std::memory_order_relaxed);
        }

        std::atomic_thread_fence(std::memory_order_release);
        cell.version.store(version + 2, std::memory_order_release);

        produced_ = sequence;
        published_.value.store(sequence, std::memory_order_release);
        return {QueueStatus::success, sequence};
    }

    // Alias matching the user-facing producer sketch (q.try_push(payload)).
    [[nodiscard]] WriteResult try_push(
        const std::span<const std::byte> payload) noexcept {
        return try_publish(payload);
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] std::uint64_t published_sequence() const noexcept {
        return published_.value.load(std::memory_order_acquire);
    }

private:
    // Bounded number of seqlock retries before a consumer concludes the cell is
    // being overwritten faster than it can be read. This bound is what keeps
    // try_read non-blocking: a contended read returns `overwritten` instead of
    // spinning indefinitely.
    static constexpr int read_attempts = 64;

    struct alignas(detail::destructive_interference_size) Cell {
        // Even => stable and readable. Odd => a publish is in progress. The
        // value increases by two per (re)write, so a consumer that observes the
        // same even version before and after its copy is guaranteed an untorn,
        // single-generation read.
        std::atomic<std::uint64_t> version{0};
        // The following members are state guarded by the seqlock `version` above.
        // They are relaxed atomics (not plain memory) so that a consumer copy
        // overlapping a producer write is always a well-defined atomic access:
        // there is no data race and no torn-read undefined behaviour, and the
        // post-copy version re-check decides whether the snapshot is usable.
        std::atomic<std::uint64_t> sequence{0};
        std::atomic<std::size_t> size{0};
        std::array<std::atomic<std::byte>, MaxPayloadSize> storage{};
    };

    static_assert(alignof(Cell) >= detail::destructive_interference_size);
    static_assert(sizeof(Cell) % detail::destructive_interference_size == 0);
    static_assert(detail::padded_atomic_layout_is_valid<std::uint64_t>);

    [[nodiscard]] static std::size_t validate_capacity(const std::size_t capacity) {
        if (capacity == 0) {
            throw std::invalid_argument(
                "VersionedSPMCQueue capacity must be greater than zero");
        }
        return capacity;
    }

    [[nodiscard]] ReadResult read(
        Consumer& consumer, const std::span<std::byte> destination) noexcept {
        const auto published = published_.value.load(std::memory_order_acquire);
        if (consumer.next_sequence_ > published) {
            return {QueueStatus::empty, 0, consumer.next_sequence_};
        }

        const auto oldest = oldest_retained(published);
        if (consumer.next_sequence_ < oldest) {
            consumer.next_sequence_ = oldest;
            return {QueueStatus::consumer_lagged, 0, oldest};
        }

        const auto want = consumer.next_sequence_;
        Cell& cell = cells_[(want - 1) % capacity_];

        std::array<std::byte, MaxPayloadSize> scratch{};
        std::uint64_t stored_sequence = 0;
        std::size_t stored_size = 0;
        bool stable = false;
        for (int attempt = 0; attempt < read_attempts; ++attempt) {
            const auto before = cell.version.load(std::memory_order_acquire);
            if ((before & 1U) != 0U) {
                continue; // publish in progress; retry.
            }
            stored_sequence = cell.sequence.load(std::memory_order_relaxed);
            stored_size = cell.size.load(std::memory_order_relaxed);
            // Copying the whole fixed-size buffer is always in-bounds, so a torn
            // read can never address memory outside the cell. The snapshot is
            // only consumed if the post-copy version check below succeeds.
            for (std::size_t index = 0; index < MaxPayloadSize; ++index) {
                scratch[index] = cell.storage[index].load(std::memory_order_relaxed);
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            const auto after = cell.version.load(std::memory_order_relaxed);
            if (before == after) {
                stable = true;
                break;
            }
        }

        if (!stable) {
            // The producer is lapping this cell faster than the consumer can copy
            // it: the wanted sequence has been overwritten. Resume from oldest.
            const auto resume = resume_after_overwrite(want);
            consumer.next_sequence_ = resume;
            return {QueueStatus::overwritten, 0, resume};
        }

        if (stored_sequence != want) {
            // A newer generation already occupies this cell; the wanted sequence
            // was overwritten between the published check and the stable read.
            const auto resume = resume_after_overwrite(want);
            consumer.next_sequence_ = resume;
            return {QueueStatus::overwritten, 0, resume};
        }

        if (destination.size() < stored_size) {
            // Match the sibling multicast queue: an undersized destination does
            // not advance the cursor.
            return {QueueStatus::message_too_large, 0, want};
        }

        std::copy_n(scratch.begin(), stored_size, destination.begin());
        ++consumer.next_sequence_;
        return {QueueStatus::success, stored_size, want};
    }

    [[nodiscard]] std::uint64_t oldest_retained(
        const std::uint64_t published) const noexcept {
        return published >= capacity_ ? published - capacity_ + 1 : 1;
    }

    // Recovery cursor after the wanted sequence was overwritten. The cursor must
    // never move backwards (that would re-deliver already-consumed sequences), so
    // it is clamped to at least `want` while jumping forward to the freshly
    // observed oldest retained sequence.
    [[nodiscard]] std::uint64_t resume_after_overwrite(const std::uint64_t want) const noexcept {
        const auto refreshed = oldest_retained(
            published_.value.load(std::memory_order_acquire));
        return refreshed > want ? refreshed : want;
    }

    const std::size_t capacity_;
    std::unique_ptr<Cell[]> cells_;

    // Producer-private monotonically increasing publication count. Only the
    // single producer thread touches `produced_`.
    std::uint64_t produced_{0};

    // Published boundary observed by consumers. Padded to avoid false sharing
    // with neighbouring producer state; this is a cache-locality mitigation, not
    // a correctness guarantee.
    detail::PaddedAtomic<std::uint64_t> published_{};
};

} // namespace orbitqueue
