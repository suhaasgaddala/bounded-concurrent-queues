#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(ORBITQUEUE_HAVE_BOOST_LOCKFREE)
#include <boost/lockfree/queue.hpp>
#endif

#include "benchmark_support.h"
#include "orbitqueue/blocking_queue.h"
#include "orbitqueue/spmc_multicast_queue.h"
#include "orbitqueue/spsc_queue.h"

namespace {

using orbitqueue::QueueStatus;
using orbitqueue::benchmark::Result;
using orbitqueue::benchmark::SequenceTracker;

struct Payload {
    std::uint64_t sequence{};
    std::uint64_t checksum{};
};

[[nodiscard]] Payload make_payload(const std::uint64_t sequence) noexcept {
    return {sequence, ~sequence};
}

struct ConsumerMetrics {
    std::uint64_t reads{};
    std::uint64_t lagged{};
    std::uint64_t validation_errors{};
    SequenceTracker verified;

    void record(const std::uint64_t sequence, const Payload& payload) {
        ++reads;
        if (sequence != payload.sequence || payload.checksum != ~sequence ||
            !verified.record(sequence)) {
            ++validation_errors;
        }
    }
};

template <typename T>
std::span<const std::byte> bytes_of(const T& value) noexcept {
    return std::as_bytes(std::span{&value, std::size_t{1}});
}

template <typename T>
std::span<std::byte> writable_bytes_of(T& value) noexcept {
    return std::as_writable_bytes(std::span{&value, std::size_t{1}});
}

[[nodiscard]] std::uint64_t duration_count(
    const std::chrono::milliseconds duration) noexcept {
    return static_cast<std::uint64_t>(duration.count());
}

[[nodiscard]] Result aggregate_result(
    const std::string_view queue_name,
    const std::size_t consumer_count,
    const std::chrono::milliseconds duration,
    const std::uint64_t published,
    const std::vector<ConsumerMetrics>& metrics) {
    std::uint64_t aggregate_reads = 0;
    std::uint64_t lagged = 0;
    std::uint64_t validation_errors = 0;
    std::vector<SequenceTracker> trackers;
    trackers.reserve(metrics.size());
    for (const auto& metric : metrics) {
        aggregate_reads += metric.reads;
        lagged += metric.lagged;
        validation_errors += metric.validation_errors;
        trackers.push_back(metric.verified);
    }

    const auto unique = orbitqueue::benchmark::count_unique_sequences(trackers);
    return {queue_name,
            orbitqueue::benchmark::queue_capacity,
            sizeof(Payload),
            1,
            consumer_count,
            duration_count(duration),
            published,
            aggregate_reads,
            unique,
            lagged,
            validation_errors};
}

[[nodiscard]] bool print_result(const Result& result) {
    std::cout << orbitqueue::benchmark::to_json(result) << '\n';
    return result.validation_errors == 0;
}

[[nodiscard]] bool benchmark_spsc(const std::chrono::milliseconds duration) {
    orbitqueue::SPSCQueue<sizeof(Payload)> queue(
        orbitqueue::benchmark::queue_capacity);
    std::atomic<bool> running{true};
    std::uint64_t published = 0;
    ConsumerMetrics metrics;

    std::thread producer([&] {
        std::uint64_t sequence = 1;
        while (running.load(std::memory_order_relaxed)) {
            const auto payload = make_payload(sequence);
            const auto result = queue.try_push(bytes_of(payload));
            if (result.status == QueueStatus::success) {
                published = result.sequence;
                ++sequence;
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&] {
        while (running.load(std::memory_order_relaxed) || !queue.empty()) {
            Payload payload;
            const auto result = queue.try_pop(writable_bytes_of(payload));
            if (result.status == QueueStatus::success) {
                metrics.record(result.sequence, payload);
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::this_thread::sleep_for(duration);
    running.store(false, std::memory_order_relaxed);
    producer.join();
    consumer.join();

    std::vector<ConsumerMetrics> all_metrics;
    all_metrics.push_back(std::move(metrics));
    return print_result(aggregate_result("spsc", 1, duration, published, all_metrics));
}

[[nodiscard]] bool benchmark_multicast(
    const std::chrono::milliseconds duration,
    const std::size_t consumer_count) {
    orbitqueue::SPMCMulticastQueue<sizeof(Payload)> queue(
        orbitqueue::benchmark::queue_capacity);
    std::atomic<bool> running{true};
    std::vector<ConsumerMetrics> metrics(consumer_count);
    std::vector<std::thread> consumers;
    consumers.reserve(consumer_count);

    for (std::size_t index = 0; index < consumer_count; ++index) {
        auto consumer = queue.register_consumer();
        consumers.emplace_back(
            [&, index, consumer = std::move(consumer)]() mutable {
                while (running.load(std::memory_order_relaxed) ||
                       consumer.next_sequence() <= queue.published_sequence()) {
                    Payload payload;
                    const auto result = consumer.try_read(writable_bytes_of(payload));
                    if (result.status == QueueStatus::success) {
                        metrics[index].record(result.sequence, payload);
                    } else if (result.status == QueueStatus::consumer_lagged ||
                               result.status == QueueStatus::overwritten) {
                        ++metrics[index].lagged;
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
    }

    std::thread producer([&] {
        std::uint64_t sequence = 1;
        while (running.load(std::memory_order_relaxed)) {
            const auto payload = make_payload(sequence);
            const auto result = queue.try_publish(bytes_of(payload));
            if (result.status == QueueStatus::success) {
                ++sequence;
            }
        }
    });

    std::this_thread::sleep_for(duration);
    running.store(false, std::memory_order_relaxed);
    producer.join();
    for (auto& consumer : consumers) {
        consumer.join();
    }

    return print_result(aggregate_result(
        "spmc_multicast", consumer_count, duration,
        queue.published_sequence(), metrics));
}

[[nodiscard]] bool benchmark_blocking(
    const std::chrono::milliseconds duration,
    const std::size_t consumer_count) {
    orbitqueue::BlockingQueue<Payload> queue(
        orbitqueue::benchmark::queue_capacity);
    std::atomic<bool> running{true};
    std::uint64_t published = 0;
    std::vector<ConsumerMetrics> metrics(consumer_count);
    std::vector<std::thread> consumers;
    consumers.reserve(consumer_count);

    for (std::size_t index = 0; index < consumer_count; ++index) {
        consumers.emplace_back([&, index] {
            while (const auto value = queue.pop()) {
                metrics[index].record(value->sequence, *value);
            }
        });
    }

    std::thread producer([&] {
        std::uint64_t sequence = 1;
        while (running.load(std::memory_order_relaxed)) {
            const auto payload = make_payload(sequence);
            const auto status = queue.try_push(payload);
            if (status == QueueStatus::success) {
                published = sequence;
                ++sequence;
            } else {
                std::this_thread::yield();
            }
        }
        queue.close();
    });

    std::this_thread::sleep_for(duration);
    running.store(false, std::memory_order_relaxed);
    producer.join();
    for (auto& consumer : consumers) {
        consumer.join();
    }

    auto result = aggregate_result(
        "blocking_mpmc", consumer_count, duration, published, metrics);
    if (result.aggregate_reads != published ||
        result.unique_sequences_verified != published) {
        ++result.validation_errors;
    }
    return print_result(result);
}

#if defined(ORBITQUEUE_HAVE_BOOST_LOCKFREE)
[[nodiscard]] bool benchmark_boost_lockfree(
    const std::chrono::milliseconds duration,
    const std::size_t consumer_count) {
    boost::lockfree::queue<
        Payload,
        boost::lockfree::capacity<orbitqueue::benchmark::queue_capacity>> queue;
    std::atomic<bool> running{true};
    std::atomic<bool> producer_done{false};
    std::uint64_t published = 0;
    std::vector<ConsumerMetrics> metrics(consumer_count);
    std::vector<std::thread> consumers;
    consumers.reserve(consumer_count);

    for (std::size_t index = 0; index < consumer_count; ++index) {
        consumers.emplace_back([&, index] {
            while (true) {
                Payload payload;
                if (queue.pop(payload)) {
                    metrics[index].record(payload.sequence, payload);
                } else if (producer_done.load(std::memory_order_acquire)) {
                    if (!queue.pop(payload)) {
                        break;
                    }
                    metrics[index].record(payload.sequence, payload);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::thread producer([&] {
        std::uint64_t sequence = 1;
        while (running.load(std::memory_order_relaxed)) {
            const auto payload = make_payload(sequence);
            if (queue.push(payload)) {
                published = sequence;
                ++sequence;
            } else {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(duration);
    running.store(false, std::memory_order_relaxed);
    producer.join();
    for (auto& consumer : consumers) {
        consumer.join();
    }

    auto result = aggregate_result(
        "boost_lockfree_work_sharing", consumer_count, duration, published, metrics);
    if (result.aggregate_reads != published ||
        result.unique_sequences_verified != published) {
        ++result.validation_errors;
    }
    return print_result(result);
}
#endif

[[nodiscard]] std::uint64_t parse_duration(const int argc, char** argv) {
    if (argc > 2) {
        throw std::invalid_argument("too many arguments");
    }
    if (argc == 1) {
        return 250;
    }

    std::size_t parsed = 0;
    const std::string argument(argv[1]);
    const auto duration = std::stoull(argument, &parsed);
    if (parsed != argument.size() || duration == 0) {
        throw std::invalid_argument("duration must be a positive integer");
    }
    return duration;
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t duration_ms = 0;
    try {
        duration_ms = parse_duration(argc, argv);
    } catch (const std::exception&) {
        std::cerr << "usage: orbitqueue_benchmark [duration_ms>0]\n";
        return EXIT_FAILURE;
    }

    const auto duration = std::chrono::milliseconds(duration_ms);
    bool valid = benchmark_spsc(duration);
    for (const auto consumers : orbitqueue::benchmark::consumer_matrix) {
        valid = benchmark_multicast(duration, consumers) && valid;
    }
    for (const auto consumers : orbitqueue::benchmark::consumer_matrix) {
        valid = benchmark_blocking(duration, consumers) && valid;
    }
#if defined(ORBITQUEUE_HAVE_BOOST_LOCKFREE)
    for (const auto consumers : orbitqueue::benchmark::consumer_matrix) {
        valid = benchmark_boost_lockfree(duration, consumers) && valid;
    }
#endif
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
