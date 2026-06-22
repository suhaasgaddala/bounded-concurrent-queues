#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "benchmark_support.h"
#include "test_support.h"

using orbitqueue::benchmark::Result;
using orbitqueue::benchmark::SequenceTracker;
using test_support::expect;

void run_benchmark_support_tests() {
    constexpr std::array<std::size_t, 3> expected_consumers{1, 3, 10};
    static_assert(orbitqueue::benchmark::consumer_matrix == expected_consumers);

    std::vector<SequenceTracker> trackers(2);
    expect(trackers[0].record(1) && trackers[0].record(2) && trackers[0].record(5),
           "sequence tracker must accept increasing sequences and gaps");
    expect(trackers[1].record(2) && trackers[1].record(3) && trackers[1].record(4),
           "independent sequence tracker must accept overlapping ranges");
    expect(!trackers[1].record(4) && !trackers[1].record(0),
           "sequence tracker must reject duplicates and sequence zero");
    expect(orbitqueue::benchmark::count_unique_sequences(trackers) == 5,
           "sequence range union must count unique values across consumers");

    const Result result{
        "example", 16, 8, 1, 3, 25, 100, 90, 80, 2, 1};
    const std::string expected =
        "{\"queue\":\"example\",\"capacity\":16,\"payload_size\":8,"
        "\"producer_count\":1,\"consumer_count\":3,\"duration_ms\":25,"
        "\"messages_published\":100,\"aggregate_reads\":90,"
        "\"unique_sequences_verified\":80,\"dropped_or_lagged\":2,"
        "\"validation_errors\":1}";
    expect(orbitqueue::benchmark::to_json(result) == expected,
           "benchmark result JSON schema must remain stable");
}
