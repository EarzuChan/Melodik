#include "melodick/render/render_group_planner.h"

#include <cmath>

namespace melodick::render {

RenderGroupPlanner::RenderGroupPlanner(RenderGroupingConfig config)
    : config_(config) {}

std::vector<RenderUnit> RenderGroupPlanner::plan(const std::vector<core::NoteBlob>& notes) const {
    std::vector<RenderUnit> units {};
    if (notes.empty()) {
        return units;
    }

    RenderUnit current {};
    current.span = notes.front().time;
    current.notes.push_back(notes.front());

    for (std::size_t i = 1; i < notes.size(); ++i) {
        const auto& prev = notes[i - 1];
        const auto& note = notes[i];

        const bool linked = prev.link_next.has_value() && note.link_prev.has_value()
            && prev.link_next.value() == note.id
            && note.link_prev.value() == prev.id
            && !prev.detached
            && !note.detached;
        const double gap = note.time.start_seconds - prev.time.end_seconds;
        const bool contiguous = gap <= config_.max_join_gap_seconds;

        if (linked && contiguous) {
            current.notes.push_back(note);
            current.span.end_seconds = note.time.end_seconds;
            continue;
        }

        units.push_back(std::move(current));
        current = RenderUnit {};
        current.span = note.time;
        current.notes.push_back(note);
    }

    units.push_back(std::move(current));
    return units;
}

} // namespace melodick::render
