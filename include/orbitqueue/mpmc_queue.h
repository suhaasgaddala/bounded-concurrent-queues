#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>

#include "orbitqueue/detail/cache_layout.h"
#include "orbitqueue/fixed_message.h"

namespace orbitqueue {

// A bounded sequence-cell MPMC ring based on Dmitry Vyukov's array algorithm.
// Counter exhaustion is outside the supported operating lifetime.
template <std::size_t MaxPayloadSize>
class MPMCQueue {
public:
    explicit MPMCQueue(const std::size_t capacity)
        : capacity_(validate_capacity(capacity)),
          mask_(capacity_ - 1),
          cells_(std::make_unique<Cell[]>(capacity_)) {
        for (std::size_t index = 0; index < capacity_; ++index) {
            cells_[index].sequence.store(index, std::memory_order_relaxed);
        }
    }

    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;

    [[nodiscard]] WriteResult try_push(
        const std::span<const std::byte> payload) noexcept {
        if (payload.size() > MaxPayloadSize) {
            return {QueueStatus::message_too_large, 0};
        }

        auto position = enqueue_pos_.value.load(std::memory_order_relaxed);
        Cell* cell = nullptr;
        for (;;) {
            cell = &cells_[position & mask_];
            const auto sequence = cell->sequence.load(std::memory_order_acquire);
            if (sequence == position) {
                if (enqueue_pos_.value.compare_exchange_weak(
                        position, position + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    break;
                }
            } else if (sequence < position) {
                return {QueueStatus::full, 0};
            } else {
                position = enqueue_pos_.value.load(std::memory_order_relaxed);
            }
        }

        const auto logical_sequence = static_cast<std::uint64_t>(position) + 1;
        const auto result = cell->message.assign(payload, logical_sequence);
        cell->sequence.store(position + 1, std::memory_order_release);
        return result;
    }

    [[nodiscard]] ReadResult try_pop(
        const std::span<std::byte> destination) noexcept {
        auto position = dequeue_pos_.value.load(std::memory_order_relaxed);
        Cell* cell = nullptr;
        for (;;) {
            cell = &cells_[position & mask_];
            const auto sequence = cell->sequence.load(std::memory_order_acquire);
            if (sequence == position + 1) {
                if (dequeue_pos_.value.compare_exchange_weak(
                        position, position + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    break;
                }
            } else if (sequence < position + 1) {
                return {QueueStatus::empty, 0, 0};
            } else {
                position = dequeue_pos_.value.load(std::memory_order_relaxed);
            }
        }

        // A claimed cell cannot be put back without violating dequeue order.
        const auto result = cell->message.copy_to(destination);
        cell->sequence.store(position + capacity_, std::memory_order_release);
        return result;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] bool empty() const noexcept {
        const auto position = dequeue_pos_.value.load(std::memory_order_relaxed);
        const auto& cell = cells_[position & mask_];
        const auto sequence = cell.sequence.load(std::memory_order_acquire);
        return sequence < position + 1;
    }

private:
    struct alignas(detail::destructive_interference_size) Cell {
        std::atomic<std::size_t> sequence{};
        FixedMessage<MaxPayloadSize> message;
    };

    // Cell alignment reduces false sharing between adjacent ring cells, and
    // padded counters reduce producer/consumer position-counter false sharing.
    // This is a cache-locality mitigation, not a correctness guarantee; actual
    // hardware cache-line behavior remains platform-dependent.
    static_assert(alignof(Cell) >= detail::destructive_interference_size);
    static_assert(sizeof(Cell) % detail::destructive_interference_size == 0);
    static_assert(detail::padded_atomic_layout_is_valid<std::size_t>);

    [[nodiscard]] static std::size_t validate_capacity(
        const std::size_t capacity) {
        if (capacity < 2 || (capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument(
                "MPMCQueue capacity must be a power of two greater than one");
        }
        return capacity;
    }

    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<Cell[]> cells_;
    detail::PaddedAtomic<std::size_t> enqueue_pos_{};
    detail::PaddedAtomic<std::size_t> dequeue_pos_{};
};

} // namespace orbitqueue
