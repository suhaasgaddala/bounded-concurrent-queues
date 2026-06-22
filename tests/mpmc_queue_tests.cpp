#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

#include "orbitqueue/mpmc_queue.h"
#include "test_support.h"

using orbitqueue::MPMCQueue;
using orbitqueue::QueueStatus;
using test_support::bytes_of;
using test_support::expect;
using test_support::writable_bytes_of;

namespace {

struct ConcurrentPayload {
    std::uint64_t id{};
    std::uint64_t checksum{};
};

} // namespace

void run_mpmc_queue_tests() {
    bool rejected_zero = false;
    try {
        MPMCQueue<8> invalid(0);
    } catch (const std::invalid_argument&) {
        rejected_zero = true;
    }
    expect(rejected_zero, "MPMC queue must reject zero capacity");

    MPMCQueue<8> queue(2);
    expect(queue.capacity() == 2 && !queue.closed(),
           "new MPMC queue must expose capacity and open state");
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
    expect(queue.try_pop(writable_bytes_of(output)).sequence == 1 && output == first,
           "MPMC queue must preserve FIFO in single-thread use");
    expect(queue.try_pop(writable_bytes_of(output)).sequence == 2 && output == second,
           "MPMC queue must preserve second FIFO item");

    const std::array exact{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    expect(queue.try_push(exact).status == QueueStatus::success,
           "exact maximum MPMC payload must be accepted");
    std::array<std::byte, 4> short_output{};
    expect(queue.try_pop(short_output).status == QueueStatus::message_too_large,
           "undersized MPMC destination must be rejected");
    std::array<std::byte, 8> exact_output{};
    expect(queue.try_pop(exact_output).status == QueueStatus::success &&
               exact_output == exact,
           "undersized MPMC read must not consume the message");

    const std::array<std::byte, 9> oversized{};
    expect(queue.try_push(oversized).status == QueueStatus::message_too_large,
           "oversized MPMC payload must be rejected");

    MPMCQueue<8> closing(2);
    expect(closing.try_push(bytes_of(first)).status == QueueStatus::success,
           "MPMC close/drain setup push must succeed");
    closing.close();
    closing.close();
    expect(closing.closed(), "MPMC close must be idempotent");
    expect(closing.try_push(bytes_of(second)).status == QueueStatus::closed,
           "closed MPMC queue must reject new pushes");
    expect(closing.try_pop(writable_bytes_of(output)).status == QueueStatus::success &&
               output == first,
           "closed MPMC queue must drain existing messages");
    expect(closing.try_pop(writable_bytes_of(output)).status == QueueStatus::closed,
           "drained closed MPMC queue must report closed");

    constexpr std::uint32_t producer_count = 4;
    constexpr std::uint32_t consumer_count = 4;
    constexpr std::uint64_t per_producer = 5'000;
    constexpr std::uint64_t total = producer_count * per_producer;
    MPMCQueue<sizeof(ConcurrentPayload)> concurrent(127);
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(total + 1), 0);
    std::mutex seen_mutex;
    std::atomic<std::uint32_t> remaining_producers{producer_count};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<bool> validation_error{false};

    std::vector<std::thread> consumers;
    for (std::uint32_t index = 0; index < consumer_count; ++index) {
        consumers.emplace_back([&] {
            std::uint64_t last_queue_sequence = 0;
            while (true) {
                ConcurrentPayload payload;
                const auto result = concurrent.try_pop(writable_bytes_of(payload));
                if (result.status == QueueStatus::success) {
                    if (result.sequence <= last_queue_sequence ||
                        payload.id == 0 || payload.id > total ||
                        payload.checksum != ~payload.id) {
                        validation_error.store(true, std::memory_order_relaxed);
                    }
                    last_queue_sequence = result.sequence;
                    {
                        std::lock_guard lock(seen_mutex);
                        auto& value = seen[static_cast<std::size_t>(payload.id)];
                        if (value != 0U) {
                            validation_error.store(true, std::memory_order_relaxed);
                        }
                        value = 1;
                    }
                    consumed.fetch_add(1, std::memory_order_relaxed);
                } else if (result.status == QueueStatus::empty) {
                    std::this_thread::yield();
                } else if (result.status == QueueStatus::closed) {
                    break;
                } else {
                    validation_error.store(true, std::memory_order_relaxed);
                    break;
                }
            }
        });
    }

    std::vector<std::thread> producers;
    for (std::uint32_t producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            for (std::uint64_t local = 1; local <= per_producer; ++local) {
                const auto id = static_cast<std::uint64_t>(producer) *
                                per_producer + local;
                const ConcurrentPayload payload{id, ~id};
                while (concurrent.try_push(bytes_of(payload)).status ==
                       QueueStatus::full) {
                    std::this_thread::yield();
                }
            }
            if (remaining_producers.fetch_sub(1, std::memory_order_acq_rel) == 1U) {
                concurrent.close();
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }
    for (auto& consumer : consumers) {
        consumer.join();
    }

    expect(consumed.load() == total,
           "MPMC concurrent test must consume every published message");
    for (std::size_t index = 1; index < seen.size(); ++index) {
        if (seen[index] == 0U) {
            validation_error.store(true, std::memory_order_relaxed);
            break;
        }
    }
    expect(!validation_error.load(),
           "MPMC concurrent test must preserve unique payloads and queue order");
}
