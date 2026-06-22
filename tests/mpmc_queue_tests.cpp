#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "orbitqueue/mpmc_queue.h"
#include "test_support.h"

using orbitqueue::MPMCQueue;
using orbitqueue::QueueStatus;
using test_support::bytes_of;
using test_support::expect;
using test_support::writable_bytes_of;

namespace {

constexpr std::uint64_t checksum_constant = 0x4f52424954515545ULL;

static_assert(!std::is_copy_constructible_v<MPMCQueue<8>>);
static_assert(!std::is_copy_assignable_v<MPMCQueue<8>>);
static_assert(!std::is_move_constructible_v<MPMCQueue<8>>);
static_assert(!std::is_move_assignable_v<MPMCQueue<8>>);

struct ConcurrentPayload {
    std::uint32_t producer_id{};
    std::uint64_t sequence{};
    std::uint64_t checksum{};
};

[[nodiscard]] constexpr std::uint64_t checksum(
    const std::uint32_t producer_id,
    const std::uint64_t sequence) noexcept {
    return static_cast<std::uint64_t>(producer_id) ^ sequence ^ checksum_constant;
}

[[nodiscard]] bool rejects_capacity(const std::size_t capacity) {
    try {
        MPMCQueue<8> invalid(capacity);
        static_cast<void>(invalid);
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

} // namespace

void run_mpmc_queue_tests() {
    expect(rejects_capacity(0), "MPMC queue must reject zero capacity");
    expect(rejects_capacity(1), "MPMC queue must reject capacity one");
    expect(rejects_capacity(3),
           "MPMC queue must reject non-power-of-two capacity");

    MPMCQueue<8> queue(2);
    expect(queue.capacity() == 2 && queue.empty(),
           "new MPMC queue must expose capacity and empty state");
    std::uint64_t output = 0;
    expect(queue.try_pop(writable_bytes_of(output)).status == QueueStatus::empty,
           "new MPMC queue must report empty");

    const std::uint64_t first = 11;
    const std::uint64_t second = 22;
    expect(queue.try_push(bytes_of(first)).sequence == 1,
           "first MPMC publication sequence must be one");
    expect(queue.try_push(bytes_of(second)).sequence == 2,
           "second MPMC publication must advance sequence");
    expect(queue.try_push(bytes_of(first)).status == QueueStatus::full,
           "bounded MPMC queue must report full without overwrite");
    expect(!queue.empty(), "populated MPMC queue must not report empty");
    expect(queue.try_pop(writable_bytes_of(output)).sequence == 1 && output == first,
           "MPMC queue must preserve FIFO in single-thread use");
    expect(queue.try_pop(writable_bytes_of(output)).sequence == 2 && output == second,
           "MPMC queue must preserve second FIFO item");

    const std::array exact{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    expect(queue.try_push(exact).sequence == 3,
           "exact maximum MPMC payload must be accepted");
    std::array<std::byte, 8> exact_output{};
    const auto exact_read = queue.try_pop(exact_output);
    expect(exact_read.status == QueueStatus::success &&
               exact_read.bytes_read == exact.size() &&
               exact_read.sequence == 3 && exact_output == exact,
           "exact maximum MPMC payload must round-trip without corruption");

    expect(queue.try_push(exact).sequence == 4,
           "MPMC short-read setup push must succeed");
    std::array<std::byte, 4> short_output{};
    const auto short_read = queue.try_pop(short_output);
    expect(short_read.status == QueueStatus::message_too_large &&
               short_read.bytes_read == 0 && short_read.sequence == 4,
           "short MPMC read must report and consume the claimed message");
    expect(queue.try_pop(exact_output).status == QueueStatus::empty,
           "short MPMC read must release rather than restore the claimed cell");

    const std::array<std::byte, 9> oversized{};
    expect(queue.try_push(oversized).status == QueueStatus::message_too_large,
           "oversized MPMC payload must be rejected");

    for (std::uint64_t sequence = 5; sequence <= 36; ++sequence) {
        const auto write = queue.try_push(bytes_of(sequence));
        const auto read = queue.try_pop(writable_bytes_of(output));
        expect(write.status == QueueStatus::success &&
                   write.sequence == sequence &&
                   read.status == QueueStatus::success &&
                   read.sequence == sequence && output == sequence,
               "MPMC wraparound must preserve payload and logical sequence");
    }

    constexpr std::uint32_t producer_count = 4;
    constexpr std::uint32_t consumer_count = 4;
    constexpr std::uint64_t per_producer = 12'500;
    constexpr std::uint64_t total = producer_count * per_producer;
    MPMCQueue<sizeof(ConcurrentPayload)> concurrent(1024);
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(total), 0);
    std::vector<std::uint64_t> published_by_queue_sequence(
        static_cast<std::size_t>(total + 1), 0);
    std::vector<std::uint64_t> consumed_by_queue_sequence(
        static_cast<std::size_t>(total + 1), 0);
    std::mutex seen_mutex;
    std::atomic<std::uint32_t> remaining_producers{producer_count};
    std::atomic<std::uint64_t> published{0};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<bool> validation_error{false};

    std::vector<std::thread> consumers;
    for (std::uint32_t index = 0; index < consumer_count; ++index) {
        consumers.emplace_back([&] {
            while (remaining_producers.load(std::memory_order_acquire) != 0 ||
                   consumed.load(std::memory_order_relaxed) <
                       published.load(std::memory_order_relaxed)) {
                ConcurrentPayload payload;
                const auto result = concurrent.try_pop(writable_bytes_of(payload));
                if (result.status == QueueStatus::empty) {
                    std::this_thread::yield();
                    continue;
                }
                if (result.status != QueueStatus::success ||
                    payload.producer_id >= producer_count ||
                    payload.sequence == 0 || payload.sequence > per_producer ||
                    payload.checksum != checksum(
                        payload.producer_id, payload.sequence) ||
                    result.sequence == 0 || result.sequence > total) {
                    validation_error.store(true, std::memory_order_relaxed);
                    if (result.status == QueueStatus::success ||
                        result.status == QueueStatus::message_too_large) {
                        consumed.fetch_add(1, std::memory_order_relaxed);
                    }
                    continue;
                }

                const auto id = static_cast<std::uint64_t>(payload.producer_id) *
                                    per_producer +
                                payload.sequence - 1;
                {
                    std::lock_guard lock(seen_mutex);
                    auto& payload_seen = seen[static_cast<std::size_t>(id)];
                    auto& queue_sequence = consumed_by_queue_sequence[
                        static_cast<std::size_t>(result.sequence)];
                    if (payload_seen != 0U || queue_sequence != 0) {
                        validation_error.store(true, std::memory_order_relaxed);
                    }
                    payload_seen = 1;
                    queue_sequence = id + 1;
                }
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::vector<std::thread> producers;
    for (std::uint32_t producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            for (std::uint64_t sequence = 1; sequence <= per_producer; ++sequence) {
                const ConcurrentPayload payload{
                    producer, sequence, checksum(producer, sequence)};
                for (;;) {
                    const auto result = concurrent.try_push(bytes_of(payload));
                    if (result.status == QueueStatus::success) {
                        const auto id = static_cast<std::uint64_t>(producer) *
                                            per_producer +
                                        sequence;
                        {
                            std::lock_guard lock(seen_mutex);
                            if (result.sequence == 0 || result.sequence > total ||
                                published_by_queue_sequence[
                                    static_cast<std::size_t>(result.sequence)] != 0) {
                                validation_error.store(
                                    true, std::memory_order_relaxed);
                            } else {
                                published_by_queue_sequence[
                                    static_cast<std::size_t>(result.sequence)] = id;
                            }
                        }
                        published.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    if (result.status != QueueStatus::full) {
                        validation_error.store(true, std::memory_order_relaxed);
                        break;
                    }
                    std::this_thread::yield();
                }
            }
            remaining_producers.fetch_sub(1, std::memory_order_acq_rel);
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }
    for (auto& consumer : consumers) {
        consumer.join();
    }

    expect(published.load() == total && consumed.load() == total,
           "MPMC high-contention test must publish and consume 50000 messages");
    for (const auto value : seen) {
        if (value == 0U) {
            validation_error.store(true, std::memory_order_relaxed);
            break;
        }
    }
    for (std::size_t sequence = 1;
         sequence < consumed_by_queue_sequence.size(); ++sequence) {
        if (consumed_by_queue_sequence[sequence] == 0 ||
            consumed_by_queue_sequence[sequence] !=
                published_by_queue_sequence[sequence]) {
            validation_error.store(true, std::memory_order_relaxed);
            break;
        }
    }
    expect(!validation_error.load(),
           "MPMC high-contention test must prevent loss, duplication, and corruption");
    expect(concurrent.empty(), "drained MPMC queue must report empty");
}
