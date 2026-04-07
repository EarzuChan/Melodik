#include "melodick/render/dirty_timeline.h"

#include <algorithm>

namespace melodick::render {

DirtyTimeline::DirtyTimeline(double total_duration_seconds)
    : total_duration_seconds_(std::max(0.0, total_duration_seconds)) {}

void DirtyTimeline::reset(double total_duration_seconds) {
    total_duration_seconds_ = std::max(0.0, total_duration_seconds);
    dirty_.clear();
}

void DirtyTimeline::mark_dirty(core::TimeRange range) {
    range.start_seconds = std::max(0.0, range.start_seconds);
    range.end_seconds = std::min(total_duration_seconds_, std::max(range.start_seconds, range.end_seconds));
    if (!range.is_valid() || range.length() <= 0.0) {
        return;
    }
    dirty_.push_back(range);
    normalize();
}

void DirtyTimeline::mark_clean(core::TimeRange range) {
    if (dirty_.empty()) {
        return;
    }

    std::vector<core::TimeRange> updated {};
    for (const auto& item : dirty_) {
        if (!item.overlaps(range)) {
            updated.push_back(item);
            continue;
        }

        if (range.start_seconds > item.start_seconds) {
            updated.push_back(core::TimeRange {.start_seconds = item.start_seconds, .end_seconds = range.start_seconds});
        }
        if (range.end_seconds < item.end_seconds) {
            updated.push_back(core::TimeRange {.start_seconds = range.end_seconds, .end_seconds = item.end_seconds});
        }
    }
    dirty_ = std::move(updated);
    normalize();
}

bool DirtyTimeline::is_dirty_at(double seconds) const {
    return std::any_of(dirty_.begin(), dirty_.end(), [&](const core::TimeRange& range) { return range.contains(seconds); });
}

std::optional<core::TimeRange> DirtyTimeline::first_dirty_from(double seconds) const {
    for (const auto& range : dirty_) {
        if (range.end_seconds < seconds) {
            continue;
        }
        return range;
    }
    return std::nullopt;
}

void DirtyTimeline::normalize() {
    if (dirty_.empty()) {
        return;
    }

    std::sort(dirty_.begin(), dirty_.end(), [](const core::TimeRange& lhs, const core::TimeRange& rhs) {
        return lhs.start_seconds < rhs.start_seconds;
    });

    std::vector<core::TimeRange> merged {};
    merged.push_back(dirty_.front());
    for (std::size_t i = 1; i < dirty_.size(); ++i) {
        auto& back = merged.back();
        const auto& current = dirty_[i];
        if (current.start_seconds <= back.end_seconds) {
            back.end_seconds = std::max(back.end_seconds, current.end_seconds);
        } else {
            merged.push_back(current);
        }
    }
    dirty_ = std::move(merged);
}

} // namespace melodick::render

