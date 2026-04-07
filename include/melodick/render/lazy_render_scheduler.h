#pragma once

#include <cstddef>
#include <vector>

#include "melodick/render/dirty_timeline.h"
#include "melodick/render/render_group_planner.h"

namespace melodick::render {

class LazyRenderScheduler {
public:
    [[nodiscard]] std::vector<RenderUnit> plan_from_playhead(
        double playhead_seconds,
        const std::vector<RenderUnit>& units,
        const DirtyTimeline& dirty,
        std::size_t max_units) const;
};

} // namespace melodick::render

