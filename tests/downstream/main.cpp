#include <array>
#include <cstddef>
#include <span>

#include "orbitqueue/spsc_queue.h"
#include "orbitqueue/mpmc_queue.h"
#include "orbitqueue/version.h"

int main() {
    static_assert(orbitqueue::version_major == 2);

    orbitqueue::SPSCQueue<4> queue(1);
    const std::array payload{std::byte{0x2A}};
    std::array<std::byte, 4> destination{};

    const auto written = queue.try_push(std::span<const std::byte>{payload});
    const auto read = queue.try_pop(std::span<std::byte>{destination});

    orbitqueue::MPMCQueue<4> mpmc(1);
    const auto mpmc_written = mpmc.try_push(std::span<const std::byte>{payload});
    const auto mpmc_read = mpmc.try_pop(std::span<std::byte>{destination});

    return written.status == orbitqueue::QueueStatus::success &&
            read.status == orbitqueue::QueueStatus::success &&
            read.bytes_read == payload.size() &&
            destination.front() == payload.front() &&
            mpmc_written.status == orbitqueue::QueueStatus::success &&
            mpmc_read.status == orbitqueue::QueueStatus::success
        ? 0
        : 1;
}
