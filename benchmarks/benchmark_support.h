#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orbitqueue::benchmark {

inline constexpr std::size_t queue_capacity = 1024;
inline constexpr std::array<std::size_t, 3> consumer_matrix{1, 3, 10};

struct SequenceRange {
    std::uint64_t first{};
    std::uint64_t last{};
};

class SequenceTracker {
public:
    [[nodiscard]] bool record(const std::uint64_t sequence) {
        if (sequence == 0) {
            return false;
        }
        if (ranges_.empty() ||
            (ranges_.back().last != std::numeric_limits<std::uint64_t>::max() &&
             sequence > ranges_.back().last + 1)) {
            ranges_.push_back({sequence, sequence});
            return true;
        }
        if (ranges_.back().last != std::numeric_limits<std::uint64_t>::max() &&
            sequence == ranges_.back().last + 1) {
            ranges_.back().last = sequence;
            return true;
        }
        return false;
    }

    [[nodiscard]] const std::vector<SequenceRange>& ranges() const noexcept {
        return ranges_;
    }

private:
    std::vector<SequenceRange> ranges_;
};

[[nodiscard]] inline std::uint64_t count_unique_sequences(
    const std::vector<SequenceTracker>& trackers) {
    std::vector<SequenceRange> ranges;
    for (const auto& tracker : trackers) {
        ranges.insert(ranges.end(), tracker.ranges().begin(), tracker.ranges().end());
    }
    if (ranges.empty()) {
        return 0;
    }

    std::sort(ranges.begin(), ranges.end(), [](const auto& left, const auto& right) {
        return left.first < right.first ||
               (left.first == right.first && left.last < right.last);
    });

    auto current = ranges.front();
    std::uint64_t count = 0;
    for (std::size_t index = 1; index < ranges.size(); ++index) {
        const auto& range = ranges[index];
        if (current.last != std::numeric_limits<std::uint64_t>::max() &&
            range.first > current.last + 1) {
            count += current.last - current.first + 1;
            current = range;
        } else if (range.last > current.last) {
            current.last = range.last;
        }
    }
    return count + current.last - current.first + 1;
}

struct Result {
    std::string_view queue;
    std::size_t capacity{};
    std::size_t payload_size{};
    std::size_t producer_count{};
    std::size_t consumer_count{};
    std::uint64_t duration_ms{};
    std::uint64_t messages_published{};
    std::uint64_t aggregate_reads{};
    std::uint64_t unique_sequences_verified{};
    std::uint64_t dropped_or_lagged{};
    std::uint64_t validation_errors{};
};

[[nodiscard]] inline std::string to_json(const Result& result) {
    std::ostringstream output;
    output << "{\"queue\":\"" << result.queue
           << "\",\"capacity\":" << result.capacity
           << ",\"payload_size\":" << result.payload_size
           << ",\"producer_count\":" << result.producer_count
           << ",\"consumer_count\":" << result.consumer_count
           << ",\"duration_ms\":" << result.duration_ms
           << ",\"messages_published\":" << result.messages_published
           << ",\"aggregate_reads\":" << result.aggregate_reads
           << ",\"unique_sequences_verified\":"
           << result.unique_sequences_verified
           << ",\"dropped_or_lagged\":" << result.dropped_or_lagged
           << ",\"validation_errors\":" << result.validation_errors << '}';
    return output.str();
}

} // namespace orbitqueue::benchmark
