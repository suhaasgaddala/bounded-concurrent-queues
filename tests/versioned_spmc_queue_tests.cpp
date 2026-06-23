#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "orbitqueue/result.h"
#include "orbitqueue/versioned_spmc_queue.h"
#include "test_support.h"

using orbitqueue::QueueStatus;
using orbitqueue::ReadResult;
using orbitqueue::VersionedSPMCQueue;
using test_support::bytes_of;
using test_support::expect;
using test_support::writable_bytes_of;

namespace {

void run_construction_and_validation() {
    bool rejected_zero = false;
    try {
        VersionedSPMCQueue<8> invalid(0);
    } catch (const std::invalid_argument&) {
        rejected_zero = true;
    }
    expect(rejected_zero, "versioned SPMC queue must reject zero capacity");

    VersionedSPMCQueue<8> queue(4);
    expect(queue.capacity() == 4, "versioned SPMC queue must report capacity");
    expect(queue.published_sequence() == 0,
           "fresh versioned SPMC queue must have no publications");
}

void run_single_consumer_roundtrip() {
    VersionedSPMCQueue<8> queue(4);
    auto consumer = queue.make_consumer();
    std::uint64_t output = 0;
    expect(consumer.try_read(writable_bytes_of(output)).status == QueueStatus::empty,
           "new versioned consumer must observe empty");

    const std::uint64_t first = 101;
    const std::uint64_t second = 202;
    expect(queue.try_publish(bytes_of(first)).sequence == 1,
           "first versioned publication must use sequence one");
    expect(queue.try_publish(bytes_of(second)).sequence == 2,
           "second versioned publication must advance the sequence");
    expect(queue.published_sequence() == 2,
           "published sequence must track the producer");

    expect(consumer.try_read(writable_bytes_of(output)).sequence == 1 && output == first,
           "versioned consumer must observe the first publication");
    expect(consumer.try_read(writable_bytes_of(output)).sequence == 2 && output == second,
           "versioned consumer must advance to the second publication");
    expect(consumer.try_read(writable_bytes_of(output)).status == QueueStatus::empty,
           "caught-up versioned consumer must report empty");
}

void run_multicast_observation() {
    VersionedSPMCQueue<8> queue(4);
    auto first_consumer = queue.make_consumer();
    auto second_consumer = queue.register_consumer();
    const std::uint64_t first = 11;
    const std::uint64_t second = 22;
    expect(queue.try_push(bytes_of(first)).status == QueueStatus::success,
           "versioned try_push alias must publish");
    expect(queue.try_publish(bytes_of(second)).status == QueueStatus::success,
           "versioned publish must succeed");

    std::uint64_t output = 0;
    expect(first_consumer.try_read(writable_bytes_of(output)).sequence == 1 && output == first,
           "first consumer must observe first publication");
    expect(first_consumer.try_pop(writable_bytes_of(output)).sequence == 2 && output == second,
           "first consumer try_pop alias must advance independently");
    expect(second_consumer.try_read(writable_bytes_of(output)).sequence == 1 && output == first,
           "second consumer must also observe first publication (multicast)");
    expect(second_consumer.try_read(writable_bytes_of(output)).sequence == 2 && output == second,
           "second consumer must also observe second publication (multicast)");
    expect(second_consumer.try_read(writable_bytes_of(output)).status == QueueStatus::empty,
           "caught-up multicast consumer must report empty");
}

void run_lag_and_wraparound() {
    VersionedSPMCQueue<8> queue(2);
    auto slow = queue.make_consumer();
    for (std::uint64_t value = 1; value <= 3; ++value) {
        expect(queue.try_publish(bytes_of(value)).status == QueueStatus::success,
               "versioned publication must succeed through wraparound");
    }

    std::uint64_t output = 0;
    const auto lagged = slow.try_read(writable_bytes_of(output));
    expect((lagged.status == QueueStatus::consumer_lagged ||
            lagged.status == QueueStatus::overwritten) &&
               slow.next_sequence() == 2,
           "slow versioned consumer must detect lag and move to oldest retained");
    expect(slow.try_read(writable_bytes_of(output)).sequence == 2 && output == 2,
           "lagged versioned consumer must continue at oldest retained message");
    expect(slow.try_read(writable_bytes_of(output)).sequence == 3 && output == 3,
           "lagged versioned consumer must continue through wraparound");
    expect(slow.try_read(writable_bytes_of(output)).status == QueueStatus::empty,
           "caught-up versioned consumer must report empty after wraparound");
}

void run_moved_consumer() {
    VersionedSPMCQueue<8> queue(2);
    for (std::uint64_t value = 1; value <= 3; ++value) {
        expect(queue.try_publish(bytes_of(value)).status == QueueStatus::success,
               "setup publication must succeed");
    }
    auto moved_from = queue.make_consumer();
    auto moved_to = std::move(moved_from);
    std::uint64_t output = 0;
    expect(moved_from.try_read(writable_bytes_of(output)).status ==
               QueueStatus::invalid_consumer,
           "moved-from versioned consumer must report invalid_consumer");
    expect(moved_to.next_sequence() == 4,
           "moved versioned consumer must preserve its cursor");
}

void run_payload_boundaries() {
    VersionedSPMCQueue<8> queue(2);
    const std::array<std::byte, 9> oversized{};
    expect(queue.try_publish(oversized).status == QueueStatus::message_too_large,
           "versioned queue must reject oversized publications");

    VersionedSPMCQueue<8> retry_queue(1);
    auto consumer = retry_queue.make_consumer();
    const std::uint64_t value = 7;
    expect(retry_queue.try_publish(bytes_of(value)).status == QueueStatus::success,
           "versioned retry setup publish must succeed");
    std::array<std::byte, 4> short_destination{};
    expect(consumer.try_read(short_destination).status == QueueStatus::message_too_large,
           "undersized versioned destination must be rejected");
    std::uint64_t output = 0;
    const auto retried = consumer.try_read(writable_bytes_of(output));
    expect(retried.status == QueueStatus::success && retried.sequence == 1 &&
               output == value,
           "undersized versioned destination must not advance the consumer");
}

void run_late_registration() {
    VersionedSPMCQueue<8> queue(4);
    const std::uint64_t first = 1;
    const std::uint64_t second = 2;
    expect(queue.try_publish(bytes_of(first)).status == QueueStatus::success,
           "pre-registration publication must succeed");
    auto late = queue.make_consumer();
    std::uint64_t output = 0;
    expect(late.try_read(writable_bytes_of(output)).status == QueueStatus::empty,
           "late versioned consumer must not replay prior history");
    expect(queue.try_publish(bytes_of(second)).status == QueueStatus::success,
           "post-registration publication must succeed");
    expect(late.try_read(writable_bytes_of(output)).sequence == 2 && output == second,
           "late versioned consumer must start at the next publication");
}

void run_zero_payload() {
    VersionedSPMCQueue<0> queue(1);
    auto consumer = queue.make_consumer();
    expect(queue.try_publish(std::span<const std::byte>{}).status == QueueStatus::success,
           "zero-byte versioned payload must be accepted");
    expect(consumer.try_read(std::span<std::byte>{}).status == QueueStatus::success,
           "zero-byte versioned payload must round trip");
}

void run_repeated_cycles() {
    VersionedSPMCQueue<8> queue(8);
    auto consumer = queue.make_consumer();
    std::uint64_t expected = 1;
    for (std::uint64_t round = 0; round < 10'000; ++round) {
        expect(queue.try_publish(bytes_of(expected)).sequence == expected,
               "repeated versioned publication must advance the sequence");
        std::uint64_t output = 0;
        const auto result = consumer.try_read(writable_bytes_of(output));
        expect(result.status == QueueStatus::success && result.sequence == expected &&
                   output == expected,
               "repeated versioned read must observe the matching payload");
        ++expected;
    }
}

// Yield-heavy stress: one producer, several consumers. Each consumer must only
// observe strictly increasing sequences (no duplicate reads), and every payload
// it reads must match the version-stamped sequence (no torn / partial reads).
void run_concurrent_stress() {
    constexpr std::uint64_t message_count = 200'000;
    constexpr std::size_t consumer_count = 4;
    VersionedSPMCQueue<8> queue(1024);

    std::atomic<bool> producer_done{false};
    std::atomic<bool> failure{false};
    std::array<std::uint64_t, consumer_count> successes{};

    std::vector<std::thread> consumers;
    for (std::size_t index = 0; index < consumer_count; ++index) {
        consumers.emplace_back([&, index] {
            auto consumer = queue.make_consumer();
            std::uint64_t last_sequence = 0;
            std::uint64_t local_successes = 0;
            while (!producer_done.load(std::memory_order_acquire) ||
                   consumer.next_sequence() <= queue.published_sequence()) {
                std::uint64_t output = 0;
                const auto result = consumer.try_read(writable_bytes_of(output));
                if (result.status == QueueStatus::success) {
                    if (result.sequence <= last_sequence || output != result.sequence) {
                        failure.store(true, std::memory_order_relaxed);
                        return;
                    }
                    last_sequence = result.sequence;
                    ++local_successes;
                } else if (result.status == QueueStatus::consumer_lagged ||
                           result.status == QueueStatus::overwritten) {
                    if (consumer.next_sequence() <= last_sequence) {
                        failure.store(true, std::memory_order_relaxed);
                        return;
                    }
                } else if (result.status == QueueStatus::empty) {
                    std::this_thread::yield();
                } else {
                    failure.store(true, std::memory_order_relaxed);
                    return;
                }
            }
            successes[index] = local_successes;
        });
    }

    std::thread producer([&] {
        for (std::uint64_t value = 1; value <= message_count; ++value) {
            const auto result = queue.try_publish(bytes_of(value));
            if (result.status != QueueStatus::success || result.sequence != value) {
                failure.store(true, std::memory_order_relaxed);
                break;
            }
            if ((value & 0x3FFU) == 0U) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    producer.join();
    for (auto& consumer : consumers) {
        consumer.join();
    }

    expect(!failure.load(),
           "versioned SPMC stress must preserve monotonic sequences and payload integrity");
    for (std::size_t index = 0; index < consumer_count; ++index) {
        expect(successes[index] > 0,
               "each versioned stress consumer must observe at least one publication");
    }
}

} // namespace

void run_versioned_spmc_queue_tests() {
    run_construction_and_validation();
    run_single_consumer_roundtrip();
    run_multicast_observation();
    run_lag_and_wraparound();
    run_moved_consumer();
    run_payload_boundaries();
    run_late_registration();
    run_zero_payload();
    run_repeated_cycles();
    run_concurrent_stress();
}
