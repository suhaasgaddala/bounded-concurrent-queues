#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "benchmark_support.h"
#include "external_baselines/benchmark_message.h"
#include "external_baselines/blocking_queue_adapter.h"
#include "external_baselines/line64_mpmc_adapter.h"
#include "external_baselines/line64_spsc_adapter.h"
#include "orbitqueue/spmc_multicast_queue.h"
#include "orbitqueue/versioned_spmc_queue.h"

#if defined(ORBITQUEUE_HAVE_BOOST_LOCKFREE_QUEUE)
#include "external_baselines/boost_lockfree_mpmc_adapter.h"
#endif
#if defined(ORBITQUEUE_HAVE_BOOST_LOCKFREE_SPSC)
#include "external_baselines/boost_lockfree_spsc_adapter.h"
#endif
#if defined(ORBITQUEUE_HAVE_MOODYCAMEL_CONCURRENTQUEUE)
#include "external_baselines/moodycamel_mpmc_adapter.h"
#endif
#if defined(ORBITQUEUE_HAVE_ATOMIC_QUEUE)
#include "external_baselines/atomic_queue_mpmc_adapter.h"
#endif
#if defined(ORBITQUEUE_HAVE_RIGTORP_SPSCQUEUE)
#include "external_baselines/rigtorp_spsc_adapter.h"
#endif

#if !defined(ORBITQUEUE_BENCHMARK_BUILD_TYPE)
#define ORBITQUEUE_BENCHMARK_BUILD_TYPE "unknown"
#endif
#if !defined(ORBITQUEUE_BENCHMARK_COMPILER)
#define ORBITQUEUE_BENCHMARK_COMPILER "unknown"
#endif
#if !defined(ORBITQUEUE_BENCHMARK_GIT_COMMIT)
#define ORBITQUEUE_BENCHMARK_GIT_COMMIT "unknown"
#endif

namespace {

using Clock = std::chrono::steady_clock;
using orbitqueue::QueueStatus;
using orbitqueue::benchmark::make_payload;
using orbitqueue::benchmark::Payload;
using orbitqueue::benchmark::read_header;
using orbitqueue::benchmark::readable;
using orbitqueue::benchmark::Result;
using orbitqueue::benchmark::SequenceTracker;
using orbitqueue::benchmark::valid_payload;
using orbitqueue::benchmark::writable;
using orbitqueue::benchmark::max_payload_size;
using orbitqueue::benchmark::payload_header_size;

struct Config {
    std::uint64_t duration_ms{250};
    std::uint64_t warmup_ms{50};
    std::uint32_t trials{3};
    std::uint32_t capacity{
        static_cast<std::uint32_t>(orbitqueue::benchmark::default_capacity)};
    std::uint32_t payload_size{64};
    std::uint32_t producers{1};
    std::vector<std::size_t> consumers{
        orbitqueue::benchmark::default_consumer_matrix.begin(),
        orbitqueue::benchmark::default_consumer_matrix.end()};
    std::string queue{"all"};
    bool custom_producers{};
    bool custom_consumers{};
};

enum class ScenarioKind {
    line64_spsc,
    boost_spsc,
    rigtorp_spsc,
    line64_blocking,
    spmc,
    versioned_spmc,
    line64_mpmc,
    boost_mpmc,
    moodycamel_mpmc,
    atomic_queue_mpmc
};

struct Scenario {
    ScenarioKind kind{};
    std::string name;
    std::string benchmark_group;
    std::string delivery_semantics;
    std::size_t producers{};
    std::size_t consumers{};
    std::string notes;
};

struct ConsumerMetrics {
    std::uint64_t reads{};
    std::uint64_t lag_events{};
    std::uint64_t empty_retries{};
    std::uint64_t invalid_payloads{};
    std::uint64_t validation_errors{};
    std::vector<std::uint64_t> payload_ids;
    SequenceTracker queue_sequences;
};

struct Metadata {
    std::string build_type;
    std::string compiler;
    std::string git_commit;
    std::string timestamp;
};

void record_payload(
    ConsumerMetrics& metrics,
    const Payload& payload,
    const std::uint32_t expected_size,
    const std::uint64_t queue_sequence = 0) {
    ++metrics.reads;
    if (!valid_payload(payload, expected_size)) {
        ++metrics.invalid_payloads;
        ++metrics.validation_errors;
        return;
    }
    metrics.payload_ids.push_back(read_header(payload).global_id);
    if (queue_sequence != 0 && !metrics.queue_sequences.record(queue_sequence)) {
        ++metrics.validation_errors;
    }
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

[[nodiscard]] std::vector<std::size_t> parse_consumers(
    const std::string_view text) {
    if (text == "default" || text == "legacy") {
        return {orbitqueue::benchmark::default_consumer_matrix.begin(),
                orbitqueue::benchmark::default_consumer_matrix.end()};
    }
    if (text == "single") {
        return {1};
    }

    std::vector<std::size_t> values;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const auto comma = text.find(',', begin);
        const auto end = comma == std::string_view::npos ? text.size() : comma;
        const auto parsed = parse_unsigned("--consumers", text.substr(begin, end - begin));
        if (parsed == 0 || parsed > 64) {
            throw std::invalid_argument("consumer counts must be between 1 and 64");
        }
        values.push_back(static_cast<std::size_t>(parsed));
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }
    if (values.empty()) {
        throw std::invalid_argument("--consumers requires a list or preset");
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

void print_usage(std::ostream& output) {
    output
        << "usage: orbitqueue_benchmark [options]\n"
        << "  --duration-ms <uint64>\n"
        << "  --warmup-ms <uint64>\n"
        << "  --trials <uint32>\n"
        << "  --capacity <uint32>\n"
        << "  --payload-size <uint32>\n"
        << "  --producers <uint32>\n"
        << "  --consumers <default|single|comma-separated counts>\n"
        << "  --queue <all|spsc|blocking|spmc|versioned_spmc|mpmc|boost|"
           "boost_spsc|boost_mpmc|moodycamel_mpmc|rigtorp_spsc|atomic_queue_mpmc>\n"
        << "  --help\n";
}

[[nodiscard]] Config parse_config(const int argc, char** argv) {
    Config config;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--help") {
            print_usage(std::cout);
            std::exit(EXIT_SUCCESS);
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument(std::string(option) + " requires a value");
        }
        const std::string_view value(argv[++index]);
        if (option == "--duration-ms") {
            config.duration_ms = parse_unsigned(option, value);
        } else if (option == "--warmup-ms") {
            config.warmup_ms = parse_unsigned(option, value);
        } else if (option == "--trials") {
            const auto parsed = parse_unsigned(option, value);
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("--trials exceeds uint32 range");
            }
            config.trials = static_cast<std::uint32_t>(parsed);
        } else if (option == "--capacity") {
            const auto parsed = parse_unsigned(option, value);
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("--capacity exceeds uint32 range");
            }
            config.capacity = static_cast<std::uint32_t>(parsed);
        } else if (option == "--payload-size") {
            const auto parsed = parse_unsigned(option, value);
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("--payload-size exceeds uint32 range");
            }
            config.payload_size = static_cast<std::uint32_t>(parsed);
        } else if (option == "--producers") {
            const auto parsed = parse_unsigned(option, value);
            if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument("--producers exceeds uint32 range");
            }
            config.producers = static_cast<std::uint32_t>(parsed);
            config.custom_producers = true;
        } else if (option == "--consumers") {
            config.consumers = parse_consumers(value);
            config.custom_consumers = true;
        } else if (option == "--queue") {
            config.queue = value;
        } else {
            throw std::invalid_argument("unknown option: " + std::string(option));
        }
    }

    const std::array<std::string_view, 12> queues{
        "all", "spsc", "blocking", "spmc", "versioned_spmc", "mpmc", "boost",
        "boost_spsc", "boost_mpmc", "moodycamel_mpmc",
        "rigtorp_spsc", "atomic_queue_mpmc"};
    if (std::find(queues.begin(), queues.end(), config.queue) == queues.end()) {
        throw std::invalid_argument("invalid --queue value");
    }
    if (config.duration_ms == 0 ||
        config.duration_ms > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument("--duration-ms must fit a positive int64");
    }
    if (config.warmup_ms > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument("--warmup-ms must fit int64");
    }
    if (config.trials == 0 || config.trials > 1000) {
        throw std::invalid_argument("--trials must be between 1 and 1000");
    }
    if (config.capacity == 0 || config.capacity > 1'000'000) {
        throw std::invalid_argument("--capacity must be between 1 and 1000000");
    }
    if ((config.queue == "all" || config.queue == "mpmc" ||
         config.queue == "boost" || config.queue == "boost_mpmc" ||
         config.queue == "moodycamel_mpmc" ||
         config.queue == "atomic_queue_mpmc") &&
        (config.capacity < 2 ||
         (config.capacity & (config.capacity - 1)) != 0)) {
        throw std::invalid_argument(
            "MPMC benchmark capacity must be a power of two greater than one");
    }
    if (config.payload_size < payload_header_size ||
        config.payload_size > max_payload_size) {
        throw std::invalid_argument("--payload-size must be between 32 and 256");
    }
    if (config.producers == 0 || config.producers > 64) {
        throw std::invalid_argument("--producers must be between 1 and 64");
    }
    return config;
}

[[nodiscard]] std::vector<Scenario> scenarios_for(const Config& config) {
    std::vector<Scenario> scenarios;
    const auto selected = [&](const std::string_view name) {
        return config.queue == "all" || config.queue == name;
    };
    const auto selected_any = [&](const std::initializer_list<std::string_view> names) {
        return std::any_of(names.begin(), names.end(), selected);
    };
    constexpr std::array<std::pair<std::size_t, std::size_t>, 4> mpmc_matrix{
        std::pair<std::size_t, std::size_t>{1, 1},
        std::pair<std::size_t, std::size_t>{2, 2},
        std::pair<std::size_t, std::size_t>{4, 4},
        std::pair<std::size_t, std::size_t>{8, 8}};
    const auto add_mpmc_scenarios = [&](
        const ScenarioKind kind,
        std::string name,
        std::string notes) {
        if (!config.custom_producers && !config.custom_consumers) {
            for (const auto [producers, consumers] : mpmc_matrix) {
                scenarios.push_back({kind, name, "mpmc_work_sharing",
                    "exclusive_pop", producers, consumers, notes});
            }
        } else {
            for (const auto consumers : config.consumers) {
                scenarios.push_back({kind, name, "mpmc_work_sharing",
                    "exclusive_pop", config.producers, consumers, notes});
            }
        }
    };

    if (selected("spsc")) {
        scenarios.push_back({ScenarioKind::line64_spsc, "Line64::SPSCQueue",
            "spsc_exclusive_handoff", "exclusive_handoff", 1, 1,
            "Line64 SPSC exclusive handoff"});
    }
    if (selected_any({"boost", "boost_spsc"})) {
#if defined(ORBITQUEUE_HAVE_BOOST_LOCKFREE_SPSC)
        scenarios.push_back({ScenarioKind::boost_spsc,
            "boost::lockfree::spsc_queue", "spsc_exclusive_handoff",
            "exclusive_handoff", 1, 1,
            "optional Boost SPSC exclusive handoff baseline"});
#else
        if (config.queue == "boost_spsc") {
            throw std::invalid_argument(
                "Boost SPSC benchmark requested but boost/lockfree/spsc_queue.hpp was unavailable at build time");
        }
#endif
    }
    if (selected("rigtorp_spsc")) {
#if defined(ORBITQUEUE_HAVE_RIGTORP_SPSCQUEUE)
        scenarios.push_back({ScenarioKind::rigtorp_spsc,
            "rigtorp::SPSCQueue", "spsc_exclusive_handoff",
            "exclusive_handoff", 1, 1,
            "optional rigtorp SPSC exclusive handoff baseline"});
#else
        if (config.queue == "rigtorp_spsc") {
            throw std::invalid_argument(
                "rigtorp SPSC benchmark requested but rigtorp/SPSCQueue.h was unavailable at build time");
        }
#endif
    }
    if (selected("spmc")) {
        for (const auto consumers : config.consumers) {
            scenarios.push_back({ScenarioKind::spmc,
                "Line64::SPMCMulticastQueue",
                "spmc_multicast_retained_history",
                "multicast_retained_history", 1, consumers,
                "SPMC multicast aggregate observations are reported separately"});
        }
    }
    if (selected("versioned_spmc")) {
        for (const auto consumers : config.consumers) {
            scenarios.push_back({ScenarioKind::versioned_spmc,
                "Line64::VersionedSPMCQueue",
                "spmc_multicast_retained_history",
                "multicast_retained_history", 1, consumers,
                "atomic-versioned mutex-free SPMC multicast; aggregate "
                "observations are reported separately"});
        }
    }
    if (selected("blocking")) {
        add_mpmc_scenarios(ScenarioKind::line64_blocking,
            "Line64::BlockingQueue",
            "Line64 blocking MPMC exclusive-pop work-sharing baseline");
    }
    if (selected("mpmc")) {
        add_mpmc_scenarios(ScenarioKind::line64_mpmc,
            "Line64::MPMCQueue",
            "Line64 mutex-free bounded sequence-cell MPMC");
    }
    if (selected_any({"boost", "boost_mpmc"})) {
#if defined(ORBITQUEUE_HAVE_BOOST_LOCKFREE_QUEUE)
        add_mpmc_scenarios(ScenarioKind::boost_mpmc,
            "boost::lockfree::queue",
            "optional Boost.Lockfree exclusive-pop MPMC baseline");
#else
        if (config.queue == "boost_mpmc") {
            throw std::invalid_argument(
                "Boost benchmark requested but boost/lockfree/queue.hpp was unavailable at build time");
        }
#endif
    }
    if (selected("moodycamel_mpmc")) {
#if defined(ORBITQUEUE_HAVE_MOODYCAMEL_CONCURRENTQUEUE)
        add_mpmc_scenarios(ScenarioKind::moodycamel_mpmc,
            "moodycamel::ConcurrentQueue",
            "optional moodycamel exclusive-pop MPMC baseline");
#else
        if (config.queue == "moodycamel_mpmc") {
            throw std::invalid_argument(
                "moodycamel benchmark requested but concurrentqueue.h was unavailable at build time");
        }
#endif
    }
    if (selected("atomic_queue_mpmc")) {
#if defined(ORBITQUEUE_HAVE_ATOMIC_QUEUE)
        add_mpmc_scenarios(ScenarioKind::atomic_queue_mpmc,
            "atomic_queue::AtomicQueueB2",
            "optional atomic_queue exclusive-pop MPMC baseline");
#else
        if (config.queue == "atomic_queue_mpmc") {
            throw std::invalid_argument(
                "atomic_queue benchmark requested but atomic_queue/atomic_queue.h was unavailable at build time");
        }
#endif
    }
    if (scenarios.empty()) {
        throw std::invalid_argument(
            "no benchmark scenarios were selected or compiled for --queue " +
            config.queue);
    }
    return scenarios;
}

[[nodiscard]] std::string timestamp_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto* utc = std::gmtime(&time);
    if (utc == nullptr) {
        return "unknown";
    }
    std::ostringstream output;
    output << std::put_time(utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

[[nodiscard]] std::vector<std::uint64_t> collect_payload_ids(
    const std::vector<ConsumerMetrics>& metrics) {
    std::vector<std::uint64_t> ids;
    for (const auto& metric : metrics) {
        ids.insert(ids.end(), metric.payload_ids.begin(), metric.payload_ids.end());
    }
    return ids;
}

[[nodiscard]] std::uint64_t count_unique_ids(
    std::vector<std::uint64_t> ids,
    std::uint64_t& duplicate_count) {
    if (ids.empty()) {
        duplicate_count = 0;
        return 0;
    }
    std::sort(ids.begin(), ids.end());
    const auto unique_end = std::unique(ids.begin(), ids.end());
    const auto unique = static_cast<std::uint64_t>(
        std::distance(ids.begin(), unique_end));
    duplicate_count = static_cast<std::uint64_t>(ids.size()) - unique;
    return unique;
}

[[nodiscard]] bool has_same_ids(
    std::vector<std::uint64_t> observed,
    std::vector<std::uint64_t> expected) {
    std::sort(observed.begin(), observed.end());
    std::sort(expected.begin(), expected.end());
    return observed == expected;
}

[[nodiscard]] Result make_result(
    const Scenario& scenario,
    const Config& config,
    const Metadata& metadata,
    const std::uint32_t trial,
    const std::uint64_t published,
    const std::vector<std::uint64_t>& published_ids,
    const std::vector<ConsumerMetrics>& metrics,
    const std::uint64_t full_retries,
    const std::uint64_t producer_errors,
    const bool require_complete_work_sharing) {
    Result result;
    result.benchmark_group = scenario.benchmark_group;
    result.queue = scenario.name;
    result.delivery_semantics = scenario.delivery_semantics;
    result.trial = trial;
    result.capacity = config.capacity;
    result.payload_size = config.payload_size;
    result.producer_count = scenario.producers;
    result.consumer_count = scenario.consumers;
    result.duration_ms = config.duration_ms;
    result.warmup_ms = config.warmup_ms;
    result.messages_published = published;
    result.full_retries = full_retries;
    result.build_type = metadata.build_type;
    result.compiler = metadata.compiler;
    result.git_commit = metadata.git_commit;
    result.timestamp = metadata.timestamp;
    result.notes = scenario.notes;
    result.consumer_reads.reserve(metrics.size());

    for (const auto& metric : metrics) {
        result.aggregate_reads += metric.reads;
        result.dropped_or_lagged += metric.lag_events;
        result.empty_retries += metric.empty_retries;
        result.invalid_payloads += metric.invalid_payloads;
        result.validation_errors += metric.validation_errors;
        result.consumer_reads.push_back(metric.reads);
    }
    result.validation_errors += producer_errors;

    std::uint64_t duplicates = 0;
    auto observed_ids = collect_payload_ids(metrics);
    result.unique_sequences_verified = count_unique_ids(
        observed_ids, duplicates);
    if (require_complete_work_sharing) {
        result.validation_errors += duplicates;
        if (result.aggregate_reads != published ||
            result.unique_sequences_verified != published ||
            published_ids.size() != published ||
            !has_same_ids(std::move(observed_ids), published_ids)) {
            ++result.validation_errors;
        }
    }

    const auto seconds = static_cast<double>(config.duration_ms) / 1000.0;
    result.throughput_messages_per_second =
        static_cast<double>(published) / seconds;
    result.throughput_reads_per_second =
        static_cast<double>(result.aggregate_reads) / seconds;
    result.throughput_bytes_per_second =
        static_cast<double>(result.aggregate_reads * config.payload_size) / seconds;
    result.consumer_reads_per_second.reserve(result.consumer_reads.size());
    for (const auto reads : result.consumer_reads) {
        result.consumer_reads_per_second.push_back(static_cast<double>(reads) / seconds);
    }
    return result;
}

template <typename Adapter>
[[nodiscard]] Result run_spsc_adapter(
    const Scenario& scenario,
    const Config& config,
    const Metadata& metadata,
    const std::uint32_t trial,
    const std::chrono::milliseconds duration) {
    Adapter queue(config.capacity);
    std::atomic<bool> running{true};
    std::atomic<bool> producer_done{false};
    std::uint64_t published = 0;
    std::uint64_t full_retries = 0;
    std::uint64_t producer_errors = 0;
    std::vector<std::uint64_t> published_ids;
    ConsumerMetrics metrics;

    std::thread producer([&] {
        std::uint64_t id = 1;
        while (running.load(std::memory_order_relaxed)) {
            const auto payload = make_payload(config.payload_size, 0, id, id);
            const auto result = queue.try_push(payload);
            if (result.status == QueueStatus::success) {
                if constexpr (Adapter::has_queue_sequences) {
                    if (result.sequence != id) {
                        ++producer_errors;
                    }
                }
                if (!Adapter::has_queue_sequences && result.sequence != 0) {
                    ++producer_errors;
                }
                published_ids.push_back(id);
                published = id++;
            } else if (result.status == QueueStatus::full) {
                ++full_retries;
                std::this_thread::yield();
            } else {
                ++producer_errors;
                break;
            }
        }
        producer_done.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        while (true) {
            Payload payload;
            payload.size = config.payload_size;
            const auto result = queue.try_pop(payload);
            if (result.status == QueueStatus::success) {
                record_payload(
                    metrics, payload, config.payload_size,
                    Adapter::has_queue_sequences ? result.sequence : 0);
            } else if (result.status == QueueStatus::empty) {
                if (producer_done.load(std::memory_order_acquire)) {
                    break;
                }
                ++metrics.empty_retries;
                std::this_thread::yield();
            } else {
                ++metrics.validation_errors;
                break;
            }
        }
    });

    std::this_thread::sleep_for(duration);
    running.store(false, std::memory_order_relaxed);
    producer.join();
    consumer.join();
    return make_result(scenario, config, metadata, trial, published,
                       published_ids, {std::move(metrics)}, full_retries,
                       producer_errors, true);
}

template <typename Queue>
[[nodiscard]] Result run_spmc_multicast(
    const Scenario& scenario,
    const Config& config,
    const Metadata& metadata,
    const std::uint32_t trial,
    const std::chrono::milliseconds duration) {
    Queue queue(config.capacity);
    std::atomic<bool> running{true};
    std::uint64_t producer_errors = 0;
    std::vector<ConsumerMetrics> metrics(scenario.consumers);
    std::vector<std::thread> consumers;
    for (std::size_t index = 0; index < scenario.consumers; ++index) {
        auto consumer = queue.register_consumer();
        consumers.emplace_back([&, index, consumer = std::move(consumer)]() mutable {
            while (running.load(std::memory_order_relaxed) ||
                   consumer.next_sequence() <= queue.published_sequence()) {
                Payload payload;
                payload.size = config.payload_size;
                const auto result = consumer.try_read(writable(payload));
                if (result.status == QueueStatus::success) {
                    record_payload(metrics[index], payload, config.payload_size,
                                   result.sequence);
                } else if (result.status == QueueStatus::consumer_lagged ||
                           result.status == QueueStatus::overwritten) {
                    ++metrics[index].lag_events;
                } else if (result.status == QueueStatus::empty) {
                    ++metrics[index].empty_retries;
                    std::this_thread::yield();
                } else {
                    ++metrics[index].validation_errors;
                    break;
                }
            }
        });
    }
    std::thread producer([&] {
        std::uint64_t id = 1;
        while (running.load(std::memory_order_relaxed)) {
            const auto payload = make_payload(config.payload_size, 0, id, id);
            const auto result = queue.try_publish(readable(payload));
            if (result.status == QueueStatus::success) {
                if (result.sequence != id) {
                    ++producer_errors;
                }
                ++id;
            } else {
                ++producer_errors;
            }
        }
    });

    std::this_thread::sleep_for(duration);
    running.store(false, std::memory_order_relaxed);
    producer.join();
    for (auto& consumer : consumers) {
        consumer.join();
    }
    return make_result(scenario, config, metadata, trial,
                       queue.published_sequence(), {}, metrics,
                       0, producer_errors, false);
}

template <typename Push, typename Pop, typename Close>
[[nodiscard]] Result run_work_sharing(
    Push push,
    Pop pop,
    Close close,
    const Scenario& scenario,
    const Config& config,
    const Metadata& metadata,
    const std::uint32_t trial,
    const std::chrono::milliseconds duration,
    const bool has_queue_sequences) {
    std::atomic<bool> running{true};
    std::atomic<std::uint64_t> next_id{1};
    std::atomic<std::uint64_t> published{0};
    std::atomic<std::uint64_t> full_retries{0};
    std::atomic<std::uint64_t> producer_errors{0};
    std::atomic<std::size_t> remaining{scenario.producers};
    std::mutex published_ids_mutex;
    std::vector<std::uint64_t> published_ids;
    std::vector<ConsumerMetrics> metrics(scenario.consumers);

    std::vector<std::thread> consumers;
    for (std::size_t index = 0; index < scenario.consumers; ++index) {
        consumers.emplace_back([&, index] {
            while (true) {
                Payload payload;
                payload.size = config.payload_size;
                const auto result = pop(payload);
                if (result.status == QueueStatus::success) {
                    record_payload(metrics[index], payload, config.payload_size,
                                   has_queue_sequences ? result.sequence : 0);
                } else if (result.status == QueueStatus::empty) {
                    ++metrics[index].empty_retries;
                    std::this_thread::yield();
                } else if (result.status == QueueStatus::closed) {
                    break;
                } else {
                    ++metrics[index].validation_errors;
                    break;
                }
            }
        });
    }

    std::vector<std::thread> producers;
    for (std::size_t producer = 0; producer < scenario.producers; ++producer) {
        producers.emplace_back([&, producer] {
            std::uint64_t local = 1;
            while (running.load(std::memory_order_relaxed)) {
                const auto id = next_id.fetch_add(1, std::memory_order_relaxed);
                const auto payload = make_payload(
                    config.payload_size, static_cast<std::uint32_t>(producer),
                    local++, id);
                while (true) {
                    const auto result = push(payload);
                    if (result.status == QueueStatus::success) {
                        {
                            std::lock_guard<std::mutex> lock(published_ids_mutex);
                            published_ids.push_back(id);
                        }
                        published.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    if (result.status != QueueStatus::full) {
                        producer_errors.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    full_retries.fetch_add(1, std::memory_order_relaxed);
                    if (!running.load(std::memory_order_relaxed)) {
                        break;
                    }
                    std::this_thread::yield();
                }
            }
            if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1U) {
                close();
            }
        });
    }

    std::this_thread::sleep_for(duration);
    running.store(false, std::memory_order_relaxed);
    for (auto& producer : producers) {
        producer.join();
    }
    for (auto& consumer : consumers) {
        consumer.join();
    }
    return make_result(
        scenario, config, metadata, trial,
        published.load(std::memory_order_relaxed), published_ids, metrics,
        full_retries.load(std::memory_order_relaxed),
        producer_errors.load(std::memory_order_relaxed), true);
}

template <typename Adapter>
[[nodiscard]] Result run_exclusive_pop_adapter(
    const Scenario& scenario,
    const Config& config,
    const Metadata& metadata,
    const std::uint32_t trial,
    const std::chrono::milliseconds duration) {
    Adapter queue(config.capacity);
    std::atomic<bool> producers_done{false};
    const auto push = [&](const Payload& payload) {
        return queue.try_push(payload);
    };
    const auto pop = [&](Payload& payload) {
        const auto result = queue.try_pop(payload);
        if (result.status == QueueStatus::empty &&
            producers_done.load(std::memory_order_acquire)) {
            return orbitqueue::ReadResult{QueueStatus::closed, 0, 0};
        }
        return result;
    };
    const auto close = [&] {
        producers_done.store(true, std::memory_order_release);
        if constexpr (requires(Adapter& adapter) { adapter.close(); }) {
            queue.close();
        }
    };
    return run_work_sharing(
        push, pop, close, scenario, config, metadata, trial, duration,
        Adapter::has_queue_sequences);
}

[[nodiscard]] Result run_scenario(
    const Scenario& scenario,
    const Config& config,
    const Metadata& metadata,
    const std::uint32_t trial,
    const std::chrono::milliseconds duration) {
    switch (scenario.kind) {
    case ScenarioKind::line64_spsc:
        return run_spsc_adapter<orbitqueue::benchmark::Line64SPSCAdapter>(
            scenario, config, metadata, trial, duration);
    case ScenarioKind::boost_spsc:
#if defined(ORBITQUEUE_HAVE_BOOST_LOCKFREE_SPSC)
        return run_spsc_adapter<orbitqueue::benchmark::BoostLockfreeSPSCAdapter>(
            scenario, config, metadata, trial, duration);
#else
        throw std::logic_error("Boost SPSC scenario was not compiled");
#endif
    case ScenarioKind::rigtorp_spsc:
#if defined(ORBITQUEUE_HAVE_RIGTORP_SPSCQUEUE)
        return run_spsc_adapter<orbitqueue::benchmark::RigtorpSPSCAdapter>(
            scenario, config, metadata, trial, duration);
#else
        throw std::logic_error("rigtorp SPSC scenario was not compiled");
#endif
    case ScenarioKind::line64_blocking:
        return run_exclusive_pop_adapter<
            orbitqueue::benchmark::Line64BlockingQueueAdapter>(
            scenario, config, metadata, trial, duration);
    case ScenarioKind::spmc:
        return run_spmc_multicast<
            orbitqueue::SPMCMulticastQueue<max_payload_size>>(
            scenario, config, metadata, trial, duration);
    case ScenarioKind::versioned_spmc:
        return run_spmc_multicast<
            orbitqueue::VersionedSPMCQueue<max_payload_size>>(
            scenario, config, metadata, trial, duration);
    case ScenarioKind::line64_mpmc:
        return run_exclusive_pop_adapter<
            orbitqueue::benchmark::Line64MPMCAdapter>(
            scenario, config, metadata, trial, duration);
    case ScenarioKind::boost_mpmc:
#if defined(ORBITQUEUE_HAVE_BOOST_LOCKFREE_QUEUE)
        return run_exclusive_pop_adapter<
            orbitqueue::benchmark::BoostLockfreeMPMCAdapter>(
            scenario, config, metadata, trial, duration);
#else
        throw std::logic_error("Boost MPMC scenario was not compiled");
#endif
    case ScenarioKind::moodycamel_mpmc:
#if defined(ORBITQUEUE_HAVE_MOODYCAMEL_CONCURRENTQUEUE)
        return run_exclusive_pop_adapter<
            orbitqueue::benchmark::MoodycamelMPMCAdapter>(
            scenario, config, metadata, trial, duration);
#else
        throw std::logic_error("moodycamel MPMC scenario was not compiled");
#endif
    case ScenarioKind::atomic_queue_mpmc:
#if defined(ORBITQUEUE_HAVE_ATOMIC_QUEUE)
        return run_exclusive_pop_adapter<
            orbitqueue::benchmark::AtomicQueueMPMCAdapter>(
            scenario, config, metadata, trial, duration);
#else
        throw std::logic_error("atomic_queue MPMC scenario was not compiled");
#endif
    }
    throw std::logic_error("unknown benchmark scenario");
}

} // namespace

int main(int argc, char** argv) {
    Config config;
    try {
        config = parse_config(argc, argv);
        const auto scenarios = scenarios_for(config);
        const Metadata metadata{
            ORBITQUEUE_BENCHMARK_BUILD_TYPE,
            ORBITQUEUE_BENCHMARK_COMPILER,
            ORBITQUEUE_BENCHMARK_GIT_COMMIT,
            timestamp_utc()};

        bool valid = true;
        for (const auto& scenario : scenarios) {
            if (config.warmup_ms != 0) {
                auto warmup_config = config;
                warmup_config.duration_ms = config.warmup_ms;
                const auto warmup = run_scenario(
                    scenario, warmup_config, metadata, 0,
                    std::chrono::milliseconds(config.warmup_ms));
                if (warmup.validation_errors != 0) {
                    std::cerr << "benchmark warmup validation failed: "
                              << orbitqueue::benchmark::to_json(warmup) << '\n';
                    valid = false;
                    continue;
                }
            }
            for (std::uint32_t trial = 1; trial <= config.trials; ++trial) {
                const auto result = run_scenario(
                    scenario, config, metadata, trial,
                    std::chrono::milliseconds(config.duration_ms));
                std::cout << orbitqueue::benchmark::to_json(result) << '\n';
                valid = result.validation_errors == 0 && valid;
            }
        }
        return valid ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "orbitqueue_benchmark: " << error.what() << '\n';
        print_usage(std::cerr);
        return EXIT_FAILURE;
    }
}
