#pragma once

#include <algorithm>

namespace melodick::core {

struct TimeRange {
    double start_seconds {0.0};
    double end_seconds {0.0};

    [[nodiscard]] double length() const { return std::max(0.0, end_seconds - start_seconds); }

    [[nodiscard]] bool is_valid() const { return end_seconds >= start_seconds; }

    [[nodiscard]] bool contains(double seconds) const {
        return seconds >= start_seconds && seconds <= end_seconds;
    }

    [[nodiscard]] bool overlaps(const TimeRange& other) const {
        return !(other.end_seconds <= start_seconds || other.start_seconds >= end_seconds);
    }
};

} // namespace melodick::core

