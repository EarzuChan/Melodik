#pragma once

#include <optional>
#include <vector>

#include "melodick/core/time_range.h"

namespace melodick::render {

class DirtyTimeline {
public:
    explicit DirtyTimeline(double total_duration_seconds = 0.0);

    void reset(double total_duration_seconds);
    void mark_dirty(core::TimeRange range);
    void mark_clean(core::TimeRange range);

    [[nodiscard]] bool is_dirty_at(double seconds) const;
    [[nodiscard]] std::optional<core::TimeRange> first_dirty_from(double seconds) const;
    [[nodiscard]] const std::vector<core::TimeRange>& dirty_ranges() const { return dirty_; }

private:
    double total_duration_seconds_ {0.0};
    std::vector<core::TimeRange> dirty_ {};

    void normalize();
};

} // namespace melodick::render

