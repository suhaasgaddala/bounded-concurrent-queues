#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <stdexcept>
#include <vector>

#include "orbitqueue/fixed_message.h"

namespace orbitqueue {

template <std::size_t MaxPayloadSize>
class MPMCQueue {
public:
    explicit MPMCQueue(const std::size_t capacity)
        : capacity_(capacity), slots_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("MPMCQueue capacity must be greater than zero");
        }
    }

    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;

    [[nodiscard]] WriteResult try_push(
        const std::span<const std::byte> payload) {
        if (payload.size() > MaxPayloadSize) {
            return {QueueStatus::message_too_large, 0};
        }

        std::lock_guard lock(mutex_);
        if (closed_) {
            return {QueueStatus::closed, 0};
        }
        if (size_ == capacity_) {
            return {QueueStatus::full, 0};
        }

        const auto sequence = published_sequence_ + 1;
        const auto result = slots_[head_].assign(payload, sequence);
        head_ = (head_ + 1) % capacity_;
        ++size_;
        published_sequence_ = sequence;
        return result;
    }

    [[nodiscard]] ReadResult try_pop(
        const std::span<std::byte> destination) {
        std::lock_guard lock(mutex_);
        if (size_ == 0) {
            return {closed_ ? QueueStatus::closed : QueueStatus::empty, 0, 0};
        }

        const auto result = slots_[tail_].copy_to(destination);
        if (ok(result.status)) {
            tail_ = (tail_ + 1) % capacity_;
            --size_;
        }
        return result;
    }

    void close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }

    [[nodiscard]] bool closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::vector<FixedMessage<MaxPayloadSize>> slots_;
    std::size_t head_{};
    std::size_t tail_{};
    std::size_t size_{};
    std::uint64_t published_sequence_{};
    bool closed_{};
};

} // namespace orbitqueue
