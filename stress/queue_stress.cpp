#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <future>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "orbitqueue/blocking_queue.h"
#include "orbitqueue/mpmc_queue.h"
#include "orbitqueue/spmc_multicast_queue.h"
#include "orbitqueue/spsc_queue.h"
#include "orbitqueue/versioned_spmc_queue.h"

namespace {

using Clock = std::chrono::steady_clock;
using orbitqueue::QueueStatus;

constexpr std::size_t max_payload_size = 256;
constexpr std::size_t payload_header_size = 32;

struct Config {
    std::uint64_t seed{0x4f52424954515545ULL};
    std::uint64_t duration_ms{250};
    std::uint64_t iterations{10'000};
    std::string queue{"all"};
    std::uint32_t producers{3};
    std::uint32_t consumers{3};
    std::uint32_t payload_size{64};
    std::uint32_t capacity{64};
    bool verbose{};
};

struct PayloadHeader {
    std::uint64_t global_sequence{};
    std::uint64_t local_sequence{};
    std::uint32_t producer_id{};
    std::uint32_t payload_size{};
    std::uint64_t checksum{};
};

static_assert(sizeof(PayloadHeader) == payload_header_size);

struct Failure {
    std::string queue;
    std::uint64_t seed{};
    std::uint64_t operation{};
    std::uint64_t expected_sequence{};
    std::uint64_t observed_sequence{};
    std::uint64_t expected_checksum{};
    std::uint64_t observed_checksum{};
    std::string reason;
};

class FailureRecorder {
public:
    void record(Failure failure) {
        std::lock_guard lock(mutex_);
        if (!failure_) {
            failure_ = std::move(failure);
        }
    }

    [[nodiscard]] bool failed() const {
        std::lock_guard lock(mutex_);
        return failure_.has_value();
    }

    void print() const {
        std::lock_guard lock(mutex_);
        if (!failure_) {
            return;
        }
        const auto& failure = *failure_;
        std::cerr << "stress_failure queue=" << failure.queue
                  << " seed=" << failure.seed
                  << " operation=" << failure.operation
                  << " expected_sequence=" << failure.expected_sequence
                  << " observed_sequence=" << failure.observed_sequence
                  << " expected_checksum=" << failure.expected_checksum
                  << " observed_checksum=" << failure.observed_checksum
                  << " reason=\"" << failure.reason << "\"\n";
    }

private:
    mutable std::mutex mutex_;
    std::optional<Failure> failure_;
};

[[nodiscard]] std::uint64_t mixed_seed(
    const std::uint64_t seed,
    const std::uint32_t producer_id,
    const std::uint64_t local_sequence) noexcept {
    auto value = seed ^ (static_cast<std::uint64_t>(producer_id) << 32U) ^
                 local_sequence;
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] std::vector<std::byte> make_payload(
    const Config& config,
    const std::uint32_t producer_id,
    const std::uint64_t local_sequence,
    const std::uint64_t global_sequence) {
    std::vector<std::byte> payload(config.payload_size);
    PayloadHeader header{
        global_sequence,
        local_sequence,
        producer_id,
        config.payload_size,
        mixed_seed(config.seed, producer_id, local_sequence)};
    std::memcpy(payload.data(), &header, sizeof(header));

    std::mt19937_64 random(header.checksum);
    for (std::size_t index = sizeof(header); index < payload.size(); ++index) {
        payload[index] = static_cast<std::byte>(random() & 0xffU);
    }
    return payload;
}

[[nodiscard]] PayloadHeader read_header(const std::span<const std::byte> payload) {
    PayloadHeader header;
    if (payload.size() >= sizeof(header)) {
        std::memcpy(&header, payload.data(), sizeof(header));
    }
    return header;
}

[[nodiscard]] bool validate_payload(
    const std::string_view queue,
    const Config& config,
    const std::span<const std::byte> payload,
    const std::uint32_t expected_producer,
    const std::uint64_t expected_local,
    const std::uint64_t expected_global,
    const std::uint64_t operation,
    FailureRecorder& failures) {
    const auto observed = read_header(payload);
    const auto expected = make_payload(
        config, expected_producer, expected_local, expected_global);
    const auto expected_header = read_header(expected);
    if (payload.size() != expected.size() ||
        !std::equal(payload.begin(), payload.end(), expected.begin(), expected.end())) {
        failures.record({std::string(queue),
                         config.seed,
                         operation,
                         expected_global,
                         observed.global_sequence,
                         expected_header.checksum,
                         observed.checksum,
                         "payload sequence/checksum/pattern mismatch"});
        return false;
    }
    return true;
}

[[nodiscard]] std::uint64_t parse_unsigned(
    const std::string_view option,
    const std::string_view text) {
    std::uint64_t value = 0;
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto [position, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || position != end) {
        throw std::invalid_argument(std::string(option) + " requires an unsigned integer");
    }
    return value;
}

void print_usage(std::ostream& output) {
    output
        << "usage: orbitqueue_stress [options]\n"
        << "  --seed <uint64>\n"
        << "  --duration-ms <uint64>\n"
        << "  --iterations <uint64>\n"
        << "  --queue <all|spsc|blocking|spmc|versioned_spmc|mpmc>\n"
        << "  --producers <uint32>\n"
        << "  --consumers <uint32>\n"
        << "  --payload-size <uint32>\n"
        << "  --capacity <uint32>\n"
        << "  --verbose\n";
}

[[nodiscard]] Config parse_config(const int argc, char** argv) {
    Config config;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--verbose") {
            config.verbose = true;
            continue;
        }
        if (option == "--help") {
            print_usage(std::cout);
            std::exit(EXIT_SUCCESS);
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument(std::string(option) + " requires a value");
        }
        const std::string_view value(argv[++index]);
        if (option == "--seed") {
            config.seed = parse_unsigned(option, value);
        } else if (option == "--duration-ms") {
            config.duration_ms = parse_unsigned(option, value);
        } else if (option == "--iterations") {
            config.iterations = parse_unsigned(option, value);
        } else if (option == "--queue") {
            config.queue = value;
        } else if (option == "--producers") {
            const auto parsed = parse_unsigned(option, value);
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("--producers exceeds uint32 range");
            }
            config.producers = static_cast<std::uint32_t>(parsed);
        } else if (option == "--consumers") {
            const auto parsed = parse_unsigned(option, value);
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("--consumers exceeds uint32 range");
            }
            config.consumers = static_cast<std::uint32_t>(parsed);
        } else if (option == "--payload-size") {
            const auto parsed = parse_unsigned(option, value);
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("--payload-size exceeds uint32 range");
            }
            config.payload_size = static_cast<std::uint32_t>(parsed);
        } else if (option == "--capacity") {
            const auto parsed = parse_unsigned(option, value);
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("--capacity exceeds uint32 range");
            }
            config.capacity = static_cast<std::uint32_t>(parsed);
        } else {
            throw std::invalid_argument("unknown option: " + std::string(option));
        }
    }

    const std::array<std::string_view, 6> queues{
        "all", "spsc", "blocking", "spmc", "versioned_spmc", "mpmc"};
    if (std::find(queues.begin(), queues.end(), config.queue) == queues.end()) {
        throw std::invalid_argument(
            "--queue must be all, spsc, blocking, spmc, versioned_spmc, or mpmc");
    }
    if (config.duration_ms == 0 ||
        config.duration_ms > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument("--duration-ms must fit a positive int64");
    }
    if (config.iterations == 0 || config.iterations > 10'000'000) {
        throw std::invalid_argument("--iterations must be between 1 and 10000000");
    }
    if (config.producers == 0 || config.producers > 64 ||
        config.consumers == 0 || config.consumers > 64) {
        throw std::invalid_argument("producer and consumer counts must be between 1 and 64");
    }
    if (config.payload_size < payload_header_size ||
        config.payload_size > max_payload_size) {
        throw std::invalid_argument("payload size must be between 32 and 256 bytes");
    }
    if (config.capacity == 0) {
        throw std::invalid_argument("capacity must be greater than zero");
    }
    if ((config.queue == "all" || config.queue == "mpmc") &&
        (config.capacity < 2 ||
         (config.capacity & (config.capacity - 1)) != 0)) {
        throw std::invalid_argument(
            "MPMC stress capacity must be a power of two greater than one");
    }
    if (config.queue == "spsc" &&
        (config.producers != 1 || config.consumers != 1)) {
        throw std::invalid_argument("SPSC stress requires one producer and one consumer");
    }
    if ((config.queue == "spmc" || config.queue == "versioned_spmc") &&
        config.producers != 1) {
        throw std::invalid_argument("SPMC stress requires one producer");
    }
    if (config.iterations >
        std::numeric_limits<std::uint64_t>::max() / config.producers) {
        throw std::invalid_argument("producer count times iterations overflows uint64");
    }
    return config;
}

[[nodiscard]] Clock::time_point deadline_for(const Config& config) {
    return Clock::now() + std::chrono::milliseconds(config.duration_ms);
}

void wait_for_start(const std::atomic<bool>& start) {
    while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

[[nodiscard]] bool run_spsc(const Config& config, FailureRecorder& failures) {
    orbitqueue::SPSCQueue<max_payload_size> retry_queue(1);
    const auto retry_payload = make_payload(config, 0, 1, 1);
    if (retry_queue.try_push(retry_payload).status != QueueStatus::success) {
        failures.record({"spsc", config.seed, 0, 1, 0, 0, 0,
                         "undersized retry setup push failed"});
        return false;
    }
    std::vector<std::byte> short_output(config.payload_size - 1U);
    if (retry_queue.try_pop(short_output).status != QueueStatus::message_too_large) {
        failures.record({"spsc", config.seed, 0, 1, 0, 0, 0,
                         "undersized read did not report message_too_large"});
        return false;
    }
    std::vector<std::byte> retry_output(config.payload_size);
    const auto retry_result = retry_queue.try_pop(retry_output);
    if (retry_result.status != QueueStatus::success ||
        !validate_payload("spsc", config, retry_output, 0, 1, 1, 0, failures)) {
        return false;
    }

    orbitqueue::SPSCQueue<max_payload_size> queue(config.capacity);
    const auto deadline = deadline_for(config);
    std::atomic<bool> start{false};
    std::atomic<bool> producer_done{false};
    std::atomic<std::uint64_t> published{0};
    std::uint64_t popped = 0;
    std::uint64_t full_retries = 0;
    std::uint64_t empty_retries = 0;

    std::thread producer([&] {
        std::mt19937_64 scheduling(config.seed ^ 0x53505343ULL);
        wait_for_start(start);
        for (std::uint64_t sequence = 1;
             sequence <= config.iterations && Clock::now() < deadline;) {
            const auto payload = make_payload(config, 0, sequence, sequence);
            const auto result = queue.try_push(payload);
            if (result.status == QueueStatus::success) {
                if (result.sequence != sequence) {
                    failures.record({"spsc", config.seed, sequence, sequence,
                                     result.sequence, 0, 0,
                                     "queue publication sequence mismatch"});
                    break;
                }
                published.store(sequence, std::memory_order_release);
                ++sequence;
            } else if (result.status == QueueStatus::full) {
                ++full_retries;
                std::this_thread::yield();
            } else {
                failures.record({"spsc", config.seed, sequence, sequence,
                                 result.sequence, 0, 0,
                                 "unexpected push status"});
                break;
            }
            if ((scheduling() & 0x3fU) == 0U) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::mt19937_64 scheduling(config.seed ^ 0x43535053ULL);
        wait_for_start(start);
        while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
            std::vector<std::byte> payload(config.payload_size);
            const auto result = queue.try_pop(payload);
            if (result.status == QueueStatus::success) {
                const auto expected = popped + 1;
                if (result.sequence != expected ||
                    !validate_payload(
                        "spsc", config, payload, 0, expected, expected,
                        expected, failures)) {
                    break;
                }
                ++popped;
            } else if (result.status == QueueStatus::empty) {
                ++empty_retries;
                std::this_thread::yield();
            } else {
                failures.record({"spsc", config.seed, popped + 1, popped + 1,
                                 result.sequence, 0, 0,
                                 "unexpected pop status"});
                break;
            }
            if ((scheduling() & 0x3fU) == 0U) {
                std::this_thread::yield();
            }
        }
    });

    start.store(true, std::memory_order_release);
    producer.join();
    consumer.join();

    const auto pushed = published.load(std::memory_order_acquire);
    if (popped != pushed) {
        failures.record({"spsc", config.seed, popped, pushed, popped, 0, 0,
                         "pushed and popped counts differ after drain"});
    }
    std::cout << "stress_result queue=spsc seed=" << config.seed
              << " pushed=" << pushed
              << " popped=" << popped
              << " full_retries=" << full_retries
              << " empty_retries=" << empty_retries
              << " validation_failures=" << (failures.failed() ? 1 : 0) << '\n';
    return !failures.failed();
}

[[nodiscard]] bool run_blocking_close_smoke(
    const Config& config,
    FailureRecorder& failures) {
    using namespace std::chrono_literals;

    orbitqueue::BlockingQueue<int> consumer_queue(1);
    auto blocked_consumer = std::async(std::launch::async, [&] {
        return consumer_queue.pop();
    });
    if (blocked_consumer.wait_for(10ms) != std::future_status::timeout) {
        failures.record({"blocking", config.seed, 0, 0, 0, 0, 0,
                         "empty consumer did not block before close"});
        return false;
    }
    consumer_queue.close();
    if (blocked_consumer.wait_for(1s) != std::future_status::ready ||
        blocked_consumer.get().has_value()) {
        failures.record({"blocking", config.seed, 0, 0, 0, 0, 0,
                         "blocked consumer did not wake empty on close"});
        return false;
    }

    orbitqueue::BlockingQueue<int> producer_queue(1);
    if (producer_queue.push(7) != QueueStatus::success) {
        failures.record({"blocking", config.seed, 0, 0, 0, 0, 0,
                         "close-race setup push failed"});
        return false;
    }
    auto blocked_producer = std::async(std::launch::async, [&] {
        return producer_queue.push(8);
    });
    if (blocked_producer.wait_for(10ms) != std::future_status::timeout) {
        failures.record({"blocking", config.seed, 0, 0, 0, 0, 0,
                         "full producer did not block before close"});
        return false;
    }
    producer_queue.close();
    if (blocked_producer.wait_for(1s) != std::future_status::ready ||
        blocked_producer.get() != QueueStatus::closed) {
        failures.record({"blocking", config.seed, 0, 0, 0, 0, 0,
                         "blocked producer did not wake closed"});
        return false;
    }
    const auto drained = producer_queue.pop();
    if (!drained || *drained != 7 || producer_queue.pop().has_value()) {
        failures.record({"blocking", config.seed, 0, 7,
                         drained ? static_cast<std::uint64_t>(*drained) : 0,
                         0, 0, "queued item was not drainable after close"});
        return false;
    }
    return true;
}

[[nodiscard]] bool run_blocking(const Config& config, FailureRecorder& failures) {
    if (!run_blocking_close_smoke(config, failures)) {
        return false;
    }

    using Payload = std::vector<std::byte>;
    orbitqueue::BlockingQueue<Payload> queue(config.capacity);
    const auto deadline = deadline_for(config);
    const auto total_slots = config.iterations * config.producers;
    std::vector<std::uint8_t> published(static_cast<std::size_t>(total_slots + 1), 0);
    std::vector<std::uint8_t> consumed(static_cast<std::size_t>(total_slots + 1), 0);
    std::mutex consumed_mutex;
    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> remaining_producers{config.producers};
    std::atomic<std::uint64_t> pushed{0};
    std::atomic<std::uint64_t> popped{0};
    std::atomic<std::uint64_t> full_retries{0};

    std::vector<std::thread> consumers;
    consumers.reserve(config.consumers);
    for (std::uint32_t consumer_id = 0; consumer_id < config.consumers; ++consumer_id) {
        consumers.emplace_back([&, consumer_id] {
            static_cast<void>(consumer_id);
            wait_for_start(start);
            while (const auto payload = queue.pop()) {
                const auto header = read_header(*payload);
                const auto global = header.global_sequence;
                const bool in_range = header.producer_id < config.producers &&
                    header.local_sequence >= 1 &&
                    header.local_sequence <= config.iterations &&
                    global == static_cast<std::uint64_t>(header.producer_id) *
                            config.iterations + header.local_sequence;
                if (!in_range || !validate_payload(
                        "blocking", config, *payload, header.producer_id,
                        header.local_sequence, global,
                        popped.load(std::memory_order_relaxed) + 1, failures)) {
                    continue;
                }
                {
                    std::lock_guard lock(consumed_mutex);
                    auto& seen = consumed[static_cast<std::size_t>(global)];
                    if (seen != 0U) {
                        failures.record({"blocking", config.seed, global,
                                         global, global, header.checksum,
                                         header.checksum,
                                         "duplicate consumed message"});
                    }
                    seen = 1;
                }
                popped.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::vector<std::thread> producers;
    producers.reserve(config.producers);
    for (std::uint32_t producer_id = 0; producer_id < config.producers; ++producer_id) {
        producers.emplace_back([&, producer_id] {
            std::mt19937_64 scheduling(
                config.seed ^ static_cast<std::uint64_t>(producer_id));
            wait_for_start(start);
            for (std::uint64_t local = 1;
                 local <= config.iterations && Clock::now() < deadline;) {
                const auto global = static_cast<std::uint64_t>(producer_id) *
                                    config.iterations + local;
                const auto payload = make_payload(config, producer_id, local, global);
                const auto status = queue.try_push(payload);
                if (status == QueueStatus::success) {
                    published[static_cast<std::size_t>(global)] = 1;
                    pushed.fetch_add(1, std::memory_order_relaxed);
                    ++local;
                } else if (status == QueueStatus::full) {
                    full_retries.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                } else {
                    failures.record({"blocking", config.seed, global,
                                     global, 0, 0, 0,
                                     "unexpected producer status"});
                    break;
                }
                if ((scheduling() & 0x7fU) == 0U) {
                    std::this_thread::yield();
                }
            }
            if (remaining_producers.fetch_sub(1, std::memory_order_acq_rel) == 1U) {
                queue.close();
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (auto& producer : producers) {
        producer.join();
    }
    for (auto& consumer : consumers) {
        consumer.join();
    }

    std::uint64_t missing = 0;
    for (std::size_t index = 1; index < published.size(); ++index) {
        if (published[index] != consumed[index]) {
            ++missing;
            if (!failures.failed()) {
                failures.record({"blocking", config.seed,
                                 static_cast<std::uint64_t>(index),
                                 published[index], consumed[index], 0, 0,
                                 "published/consumed membership mismatch"});
            }
        }
    }
    const auto pushed_count = pushed.load(std::memory_order_relaxed);
    const auto popped_count = popped.load(std::memory_order_relaxed);
    if (pushed_count != popped_count || missing != 0) {
        failures.record({"blocking", config.seed, popped_count,
                         pushed_count, popped_count, 0, 0,
                         "pushed and popped totals differ after close/drain"});
    }
    std::cout << "stress_result queue=blocking seed=" << config.seed
              << " pushed=" << pushed_count
              << " popped=" << popped_count
              << " full_retries=" << full_retries.load(std::memory_order_relaxed)
              << " missing=" << missing
              << " validation_failures=" << (failures.failed() ? 1 : 0) << '\n';
    return !failures.failed();
}

struct MulticastMetrics {
    std::uint64_t reads{};
    std::uint64_t lag_events{};
    std::uint64_t last_sequence{};
    std::vector<std::uint8_t> seen;
};

template <typename Queue>
[[nodiscard]] bool run_spmc_multicast(
    const std::string_view label,
    const Config& config,
    FailureRecorder& failures) {
    Queue queue(config.capacity);
    const auto deadline = deadline_for(config);
    std::atomic<bool> start{false};
    std::atomic<bool> producer_done{false};
    std::vector<MulticastMetrics> metrics(config.consumers);
    std::vector<std::thread> consumers;
    consumers.reserve(config.consumers);

    for (std::uint32_t index = 0; index < config.consumers; ++index) {
        metrics[index].seen.resize(static_cast<std::size_t>(config.iterations + 1), 0);
        auto consumer = queue.register_consumer();
        consumers.emplace_back(
            [&, index, consumer = std::move(consumer)]() mutable {
                wait_for_start(start);
                while (!producer_done.load(std::memory_order_acquire) ||
                       consumer.next_sequence() <= queue.published_sequence()) {
                    std::vector<std::byte> payload(config.payload_size);
                    const auto result = consumer.try_read(payload);
                    if (result.status == QueueStatus::success) {
                        const auto sequence = result.sequence;
                        if (sequence == 0 || sequence > config.iterations ||
                            sequence <= metrics[index].last_sequence) {
                            failures.record({std::string(label), config.seed, sequence,
                                             metrics[index].last_sequence + 1,
                                             sequence, 0, 0,
                                             "impossible or non-increasing sequence"});
                            continue;
                        }
                        if (!validate_payload(
                                label, config, payload, 0, sequence, sequence,
                                sequence, failures)) {
                            continue;
                        }
                        metrics[index].last_sequence = sequence;
                        metrics[index].seen[static_cast<std::size_t>(sequence)] = 1;
                        ++metrics[index].reads;
                    } else if (result.status == QueueStatus::consumer_lagged ||
                               result.status == QueueStatus::overwritten) {
                        ++metrics[index].lag_events;
                    } else if (result.status == QueueStatus::empty) {
                        std::this_thread::yield();
                    } else {
                        failures.record({std::string(label), config.seed,
                                         metrics[index].reads, 0,
                                         result.sequence, 0, 0,
                                         "unexpected consumer status"});
                    }
                }
            });
    }

    std::thread producer([&] {
        std::mt19937_64 scheduling(config.seed ^ 0x53504d43ULL);
        wait_for_start(start);
        for (std::uint64_t sequence = 1;
             sequence <= config.iterations && Clock::now() < deadline;
             ++sequence) {
            const auto payload = make_payload(config, 0, sequence, sequence);
            const auto result = queue.try_publish(payload);
            if (result.status != QueueStatus::success ||
                result.sequence != sequence) {
                failures.record({std::string(label), config.seed, sequence, sequence,
                                 result.sequence, 0, 0,
                                 "publication failed or sequence mismatched"});
                break;
            }
            if ((scheduling() & 0x7fU) == 0U) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    start.store(true, std::memory_order_release);
    producer.join();
    for (auto& consumer : consumers) {
        consumer.join();
    }

    const auto published = queue.published_sequence();
    std::uint64_t aggregate_reads = 0;
    std::uint64_t lag_events = 0;
    std::uint64_t unique = 0;
    for (const auto& metric : metrics) {
        aggregate_reads += metric.reads;
        lag_events += metric.lag_events;
    }
    for (std::uint64_t sequence = 1; sequence <= published; ++sequence) {
        bool observed = false;
        for (const auto& metric : metrics) {
            observed = observed ||
                metric.seen[static_cast<std::size_t>(sequence)] != 0U;
        }
        unique += observed ? 1U : 0U;
    }
    std::cout << "stress_result queue=" << label << " seed=" << config.seed
              << " published=" << published
              << " aggregate_reads=" << aggregate_reads
              << " unique_verified=" << unique
              << " lag_events=" << lag_events
              << " validation_failures=" << (failures.failed() ? 1 : 0) << '\n';
    return !failures.failed();
}

[[nodiscard]] bool run_mpmc(const Config& config, FailureRecorder& failures) {
    orbitqueue::MPMCQueue<max_payload_size> queue(config.capacity);
    const auto deadline = deadline_for(config);
    const auto total_slots = config.iterations * config.producers;
    std::vector<std::uint8_t> published(static_cast<std::size_t>(total_slots + 1), 0);
    std::vector<std::uint8_t> consumed(static_cast<std::size_t>(total_slots + 1), 0);
    std::mutex consumed_mutex;
    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> remaining_producers{config.producers};
    std::atomic<std::uint64_t> pushed{0};
    std::atomic<std::uint64_t> popped{0};
    std::atomic<std::uint64_t> full_retries{0};
    std::atomic<std::uint64_t> empty_retries{0};

    std::vector<std::thread> consumers;
    consumers.reserve(config.consumers);
    for (std::uint32_t consumer_id = 0; consumer_id < config.consumers; ++consumer_id) {
        consumers.emplace_back([&, consumer_id] {
            static_cast<void>(consumer_id);
            std::uint64_t last_queue_sequence = 0;
            wait_for_start(start);
            while (true) {
                std::vector<std::byte> payload(config.payload_size);
                const auto result = queue.try_pop(payload);
                if (result.status == QueueStatus::success) {
                    popped.fetch_add(1, std::memory_order_relaxed);
                    const auto header = read_header(payload);
                    const auto global = header.global_sequence;
                    const bool in_range = header.producer_id < config.producers &&
                        header.local_sequence >= 1 &&
                        header.local_sequence <= config.iterations &&
                        global == static_cast<std::uint64_t>(header.producer_id) *
                                config.iterations + header.local_sequence;
                    if (result.sequence <= last_queue_sequence) {
                        failures.record({"mpmc", config.seed, result.sequence,
                                         last_queue_sequence + 1, result.sequence,
                                         0, header.checksum,
                                         "non-increasing queue sequence"});
                    }
                    last_queue_sequence = result.sequence;
                    if (!in_range || !validate_payload(
                            "mpmc", config, payload, header.producer_id,
                            header.local_sequence, global,
                            result.sequence, failures)) {
                        continue;
                    }
                    {
                        std::lock_guard lock(consumed_mutex);
                        auto& seen = consumed[static_cast<std::size_t>(global)];
                        if (seen != 0U) {
                            failures.record({"mpmc", config.seed, result.sequence,
                                             global, global, header.checksum,
                                             header.checksum,
                                             "duplicate consumed message"});
                        }
                        seen = 1;
                    }
                } else if (result.status == QueueStatus::empty) {
                    empty_retries.fetch_add(1, std::memory_order_relaxed);
                    if (remaining_producers.load(std::memory_order_acquire) == 0 &&
                        popped.load(std::memory_order_relaxed) >=
                            pushed.load(std::memory_order_relaxed)) {
                        break;
                    }
                    std::this_thread::yield();
                } else {
                    failures.record({"mpmc", config.seed,
                                     popped.load(std::memory_order_relaxed),
                                     0, result.sequence, 0, 0,
                                     "unexpected consumer status"});
                    break;
                }
            }
        });
    }

    std::vector<std::thread> producers;
    producers.reserve(config.producers);
    for (std::uint32_t producer_id = 0; producer_id < config.producers; ++producer_id) {
        producers.emplace_back([&, producer_id] {
            std::mt19937_64 scheduling(
                config.seed ^ 0x4d504d43ULL ^
                static_cast<std::uint64_t>(producer_id));
            wait_for_start(start);
            for (std::uint64_t local = 1;
                 local <= config.iterations && Clock::now() < deadline;) {
                const auto global = static_cast<std::uint64_t>(producer_id) *
                                    config.iterations + local;
                const auto payload = make_payload(config, producer_id, local, global);
                const auto result = queue.try_push(payload);
                if (result.status == QueueStatus::success) {
                    published[static_cast<std::size_t>(global)] = 1;
                    pushed.fetch_add(1, std::memory_order_relaxed);
                    ++local;
                } else if (result.status == QueueStatus::full) {
                    full_retries.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                } else {
                    failures.record({"mpmc", config.seed, global,
                                     global, result.sequence, 0, 0,
                                     "unexpected producer status"});
                    break;
                }
                if ((scheduling() & 0x7fU) == 0U) {
                    std::this_thread::yield();
                }
            }
            remaining_producers.fetch_sub(1, std::memory_order_acq_rel);
        });
    }

    start.store(true, std::memory_order_release);
    for (auto& producer : producers) {
        producer.join();
    }
    for (auto& consumer : consumers) {
        consumer.join();
    }

    std::uint64_t missing = 0;
    for (std::size_t index = 1; index < published.size(); ++index) {
        if (published[index] != consumed[index]) {
            ++missing;
            if (!failures.failed()) {
                failures.record({"mpmc", config.seed,
                                 static_cast<std::uint64_t>(index),
                                 published[index], consumed[index], 0, 0,
                                 "published/consumed membership mismatch"});
            }
        }
    }
    const auto pushed_count = pushed.load(std::memory_order_relaxed);
    const auto popped_count = popped.load(std::memory_order_relaxed);
    if (pushed_count != popped_count || missing != 0) {
        failures.record({"mpmc", config.seed, popped_count,
                         pushed_count, popped_count, 0, 0,
                         "pushed and popped totals differ after drain"});
    }
    std::cout << "stress_result queue=mpmc seed=" << config.seed
              << " pushed=" << pushed_count
              << " popped=" << popped_count
              << " full_retries=" << full_retries.load(std::memory_order_relaxed)
              << " empty_retries=" << empty_retries.load(std::memory_order_relaxed)
              << " missing=" << missing
              << " validation_failures=" << (failures.failed() ? 1 : 0) << '\n';
    return !failures.failed();
}

[[nodiscard]] bool should_run(const Config& config, const std::string_view queue) {
    return config.queue == "all" || config.queue == queue;
}

} // namespace

int main(int argc, char** argv) {
    Config config;
    try {
        config = parse_config(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "orbitqueue_stress: " << error.what() << '\n';
        print_usage(std::cerr);
        return EXIT_FAILURE;
    }

    std::cout << "stress_config seed=" << config.seed
              << " queue=" << config.queue
              << " duration_ms=" << config.duration_ms
              << " iterations=" << config.iterations
              << " producers=" << config.producers
              << " consumers=" << config.consumers
              << " payload_size=" << config.payload_size
              << " capacity=" << config.capacity
              << " verbose=" << (config.verbose ? "true" : "false") << '\n';

    FailureRecorder failures;
    bool passed = true;
    if (should_run(config, "spsc")) {
        passed = run_spsc(config, failures) && passed;
    }
    if (should_run(config, "blocking")) {
        passed = run_blocking(config, failures) && passed;
    }
    if (should_run(config, "spmc")) {
        passed = run_spmc_multicast<
            orbitqueue::SPMCMulticastQueue<max_payload_size>>(
            "spmc", config, failures) && passed;
    }
    if (should_run(config, "versioned_spmc")) {
        passed = run_spmc_multicast<
            orbitqueue::VersionedSPMCQueue<max_payload_size>>(
            "versioned_spmc", config, failures) && passed;
    }
    if (should_run(config, "mpmc")) {
        passed = run_mpmc(config, failures) && passed;
    }

    failures.print();
    std::cout << "stress_summary seed=" << config.seed
              << " result=" << (passed && !failures.failed() ? "pass" : "fail")
              << '\n';
    return passed && !failures.failed() ? EXIT_SUCCESS : EXIT_FAILURE;
}
