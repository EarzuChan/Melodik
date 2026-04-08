#pragma once

#include <cstdint>
#include <vector>

#include "melodick/core/note_blob.h"

namespace melodick::render {

struct RenderUnit {
    core::TimeRange span;
    std::vector<std::int64_t> note_ids;
};

struct RenderGroupingConfig {
    double max_join_gap_seconds {0.005};
};

class RenderGroupPlanner {
public:
    explicit RenderGroupPlanner(RenderGroupingConfig config = {});
    [[nodiscard]] std::vector<RenderUnit> plan(const std::vector<core::NoteBlob>& notes) const;

private:
    RenderGroupingConfig config_;
};

} // namespace melodick::render
