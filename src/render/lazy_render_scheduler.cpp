#include "melodick/render/lazy_render_scheduler.h"

#include <algorithm>

namespace melodick::render {

std::vector<RenderUnit> LazyRenderScheduler::plan_from_playhead(
    double playhead_seconds,
    const std::vector<RenderUnit>& units,
    const DirtyTimeline& dirty,
    std::size_t max_units) const {

    std::vector<RenderUnit> candidates {};
    candidates.reserve(units.size());

    for (const auto& unit : units) {
        if (unit.span.end_seconds < playhead_seconds) {
            continue;
        }

        bool unit_dirty = false;
        for (const auto& range : dirty.dirty_ranges()) {
            if (range.overlaps(unit.span)) {
                unit_dirty = true;
                break;
            }
        }
        if (unit_dirty) {
            candidates.push_back(unit);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [&](const RenderUnit& lhs, const RenderUnit& rhs) {
        return lhs.span.start_seconds < rhs.span.start_seconds;
    });

    if (candidates.size() > max_units) {
        candidates.resize(max_units);
    }

    return candidates;
}

} // namespace melodick::render

