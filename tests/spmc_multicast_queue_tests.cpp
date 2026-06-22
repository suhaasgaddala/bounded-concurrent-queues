#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include "orbitqueue/spmc_multicast_queue.h"
#include "test_support.h"

using orbitqueue::QueueStatus;
using orbitqueue::SPMCMulticastQueue;
using test_support::bytes_of;
using test_support::expect;
using test_support::writable_bytes_of;

void run_spmc_multicast_queue_tests() {
    bool rejected_zero = false;
    try {
        SPMCMulticastQueue<8> invalid(0);
    } catch (const std::invalid_argument&) {
        rejected_zero = true;
    }
    expect(rejected_zero, "multicast queue must reject zero capacity");

    SPMCMulticastQueue<8> queue(3);
    auto first_consumer = queue.register_consumer();
    auto second_consumer = queue.register_consumer();
    const std::uint64_t first = 101;
    const std::uint64_t second = 202;
    expect(queue.try_publish(bytes_of(first)).sequence == 1,
           "first multicast publication must use sequence one");
    expect(queue.try_publish(bytes_of(second)).sequence == 2,
           "second multicast publication must advance sequence");

    std::uint64_t output = 0;
    expect(first_consumer.try_read(writable_bytes_of(output)).sequence == 1 && output == first,
           "first consumer must see first publication");
    expect(first_consumer.try_read(writable_bytes_of(output)).sequence == 2 && output == second,
           "first consumer must advance independently");
    expect(second_consumer.try_read(writable_bytes_of(output)).sequence == 1 && output == first,
           "second consumer must also see first publication");
    expect(second_consumer.try_read(writable_bytes_of(output)).sequence == 2 && output == second,
           "second consumer must also see second publication");
    expect(second_consumer.try_read(writable_bytes_of(output)).status == QueueStatus::empty,
           "caught-up multicast consumer must report empty");

    SPMCMulticastQueue<8> wrapping(2);
    auto slow = wrapping.register_consumer();
    for (std::uint64_t value = 1; value <= 3; ++value) {
        expect(wrapping.try_publish(bytes_of(value)).status == QueueStatus::success,
               "multicast publication must succeed");
    }
    const auto lagged = slow.try_read(writable_bytes_of(output));
    expect(lagged.status == QueueStatus::consumer_lagged && slow.next_sequence() == 2,
           "slow consumer must detect lag and move to oldest retained sequence");
    expect(slow.try_read(writable_bytes_of(output)).sequence == 2 && output == 2,
           "lagged consumer must continue at oldest retained message");
    expect(slow.try_read(writable_bytes_of(output)).sequence == 3 && output == 3,
           "lagged consumer must continue through wraparound");

    auto moved_from = wrapping.register_consumer();
    auto moved_to = std::move(moved_from);
    expect(moved_from.try_read(writable_bytes_of(output)).status ==
               QueueStatus::invalid_consumer,
           "moved-from consumer must report invalid_consumer");
    expect(moved_to.next_sequence() == 4,
           "moved consumer must preserve its cursor");

    const std::array<std::byte, 9> oversized{};
    expect(wrapping.try_publish(oversized).status == QueueStatus::message_too_large,
           "multicast queue must reject oversized messages");

    SPMCMulticastQueue<8> retry_queue(1);
    auto retry_consumer = retry_queue.register_consumer();
    expect(retry_queue.try_publish(bytes_of(first)).status == QueueStatus::success,
           "multicast retry setup publish must succeed");
    std::array<std::byte, 4> short_destination{};
    expect(retry_consumer.try_read(short_destination).status ==
               QueueStatus::message_too_large,
           "undersized multicast output must be rejected");
    output = 0;
    const auto retried = retry_consumer.try_read(writable_bytes_of(output));
    expect(retried.status == QueueStatus::success && retried.sequence == 1 &&
               output == first,
           "undersized multicast output must not advance the consumer");

    SPMCMulticastQueue<8> late_registration(2);
    expect(late_registration.try_publish(bytes_of(first)).status == QueueStatus::success,
           "pre-registration publication must succeed");
    auto late_consumer = late_registration.register_consumer();
    expect(late_consumer.try_read(writable_bytes_of(output)).status == QueueStatus::empty,
           "new multicast consumer must not replay prior history");
    expect(late_registration.try_publish(bytes_of(second)).status == QueueStatus::success,
           "post-registration publication must succeed");
    expect(late_consumer.try_read(writable_bytes_of(output)).sequence == 2 &&
               output == second,
           "new multicast consumer must start at the next publication");

    SPMCMulticastQueue<0> zero_payload_queue(1);
    auto zero_payload_consumer = zero_payload_queue.register_consumer();
    expect(zero_payload_queue.try_publish(std::span<const std::byte>{}).status ==
               QueueStatus::success,
           "zero-byte multicast payload must be accepted");
    expect(zero_payload_consumer.try_read(std::span<std::byte>{}).status ==
               QueueStatus::success,
           "zero-byte multicast payload must round trip");
}
