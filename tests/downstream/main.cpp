#include <array>
#include <cstddef>
#include <span>

#include "orbitqueue/spsc_queue.h"
#include "orbitqueue/mpmc_queue.h"
#include "orbitqueue/versioned_spmc_queue.h"
#include "orbitqueue/version.h"

int main() {
    static_assert(orbitqueue::version_major == 0);
    static_assert(orbitqueue::version_minor == 1);
    static_assert(orbitqueue::version_patch == 1);

    orbitqueue::SPSCQueue<4> queue(1);
    const std::array payload{std::byte{0x2A}};
    std::array<std::byte, 4> destination{};

    const auto written = queue.try_push(std::span<const std::byte>{payload});
    const auto read = queue.try_pop(std::span<std::byte>{destination});

    orbitqueue::MPMCQueue<4> mpmc(2);
    const auto mpmc_written = mpmc.try_push(std::span<const std::byte>{payload});
    const auto mpmc_read = mpmc.try_pop(std::span<std::byte>{destination});

    orbitqueue::VersionedSPMCQueue<4> versioned(2);
    auto reader = versioned.make_consumer();
    const auto versioned_written =
        versioned.try_publish(std::span<const std::byte>{payload});
    const auto versioned_read = reader.try_read(std::span<std::byte>{destination});

    return written.status == orbitqueue::QueueStatus::success &&
            read.status == orbitqueue::QueueStatus::success &&
            read.bytes_read == payload.size() &&
            destination.front() == payload.front() &&
            mpmc_written.status == orbitqueue::QueueStatus::success &&
            mpmc_read.status == orbitqueue::QueueStatus::success &&
            versioned_written.status == orbitqueue::QueueStatus::success &&
            versioned_read.status == orbitqueue::QueueStatus::success &&
            versioned_read.bytes_read == payload.size()
        ? 0
        : 1;
}
