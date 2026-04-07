#include "test_framework.h"

#include "melodick/render/dirty_timeline.h"
#include "melodick/render/lazy_render_scheduler.h"
#include "melodick/render/render_group_planner.h"

MELODICK_TEST(render_group_planner_merges_linked_contiguous_notes) {
    melodick::core::NoteBlob n1 {};
    n1.id = 1;
    n1.time = {.start_seconds = 0.0, .end_seconds = 0.5};
    n1.link_next = 2;

    melodick::core::NoteBlob n2 {};
    n2.id = 2;
    n2.time = {.start_seconds = 0.5, .end_seconds = 1.0};
    n2.link_prev = 1;

    melodick::render::RenderGroupPlanner planner {};
    auto units = planner.plan({n1, n2});

    MELODICK_EXPECT_EQ(units.size(), static_cast<std::size_t>(1));
    MELODICK_EXPECT_EQ(units[0].notes.size(), static_cast<std::size_t>(2));
}

MELODICK_TEST(dirty_timeline_and_scheduler_follow_playhead) {
    melodick::render::DirtyTimeline dirty {3.0};
    dirty.mark_dirty({.start_seconds = 0.0, .end_seconds = 1.5});
    dirty.mark_dirty({.start_seconds = 2.0, .end_seconds = 2.5});
    dirty.mark_clean({.start_seconds = 0.0, .end_seconds = 0.8});

    melodick::render::RenderUnit a {.span = {.start_seconds = 0.0, .end_seconds = 1.0}, .notes = {}};
    melodick::render::RenderUnit b {.span = {.start_seconds = 1.0, .end_seconds = 2.0}, .notes = {}};
    melodick::render::RenderUnit c {.span = {.start_seconds = 2.0, .end_seconds = 3.0}, .notes = {}};

    melodick::render::LazyRenderScheduler scheduler {};
    auto plan = scheduler.plan_from_playhead(1.0, {a, b, c}, dirty, 2);

    MELODICK_EXPECT_EQ(plan.size(), static_cast<std::size_t>(2));
    MELODICK_EXPECT_TRUE(plan[0].span.end_seconds >= 1.0);
    MELODICK_EXPECT_TRUE(plan[1].span.start_seconds >= plan[0].span.start_seconds);
}
