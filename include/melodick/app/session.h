#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "melodick/app/capability_chain.h"
#include "melodick/project/project_state.h"
#include "melodick/render/dirty_timeline.h"
#include "melodick/render/lazy_render_scheduler.h"
#include "melodick/render/render_group_planner.h"

namespace melodick::app {

class Session {
public:
    Session(
        std::shared_ptr<capabilities::IPitchExtractor> pitch_extractor,
        std::shared_ptr<capabilities::IVocoder> vocoder,
        capabilities::SegmenterConfig segmenter_config = {},
        render::RenderGroupingConfig render_grouping = {});

    void import_audio(const std::vector<float>& mono_samples, int sample_rate);
    void shift_blob_pitch(std::int64_t blob_id, double semitones);
    void stretch_blob_time(std::int64_t blob_id, double new_duration_seconds);
    void shift_note_pitch(std::int64_t note_id, double semitones) { shift_blob_pitch(note_id, semitones); }

    [[nodiscard]] std::vector<render::RenderUnit> plan_render_from(double playhead_seconds, std::size_t budget_units) const;
    void render_units(const std::vector<render::RenderUnit>& units);
    [[nodiscard]] std::vector<float> build_rendered_mixdown() const;
    void render_all_dirty(std::size_t budget_units = 32);

    [[nodiscard]] project::ProjectState capture_project_state(const std::string& source_audio_path = {}) const;
    void restore_project_state(const project::ProjectState& state, const std::vector<float>& mono_samples, int sample_rate);

    [[nodiscard]] const std::vector<core::NoteBlob>& blobs() const { return blobs_; }
    [[nodiscard]] const std::vector<core::NoteBlob>& notes() const { return blobs_; }
    [[nodiscard]] const core::PitchSlice& analysis_f0() const { return analysis_f0_; }
    [[nodiscard]] const render::DirtyTimeline& dirty_timeline() const { return dirty_timeline_; }
    [[nodiscard]] int sample_rate() const { return sample_rate_; }
    [[nodiscard]] double duration_seconds() const { return duration_seconds_; }

private:
    CapabilityChain chain_;
    render::RenderGroupPlanner planner_;
    render::LazyRenderScheduler scheduler_;

    std::vector<float> mono_samples_ {};
    int sample_rate_ {44100};
    double duration_seconds_ {0.0};

    std::vector<core::NoteBlob> blobs_ {};
    core::PitchSlice analysis_f0_ {};
    std::vector<render::RenderUnit> render_units_ {};
    render::DirtyTimeline dirty_timeline_ {};

    struct RenderedBlobCache {
        std::vector<float> audio {};
        std::uint64_t source_revision {0};
    };
    std::unordered_map<std::int64_t, RenderedBlobCache> rendered_blob_cache_ {};

    [[nodiscard]] const core::NoteBlob* find_blob(std::int64_t blob_id) const;
    core::NoteBlob* find_blob(std::int64_t blob_id);
    void refresh_derived_dirty();
    [[nodiscard]] bool is_unit_dirty(const render::RenderUnit& unit) const;
};

} // namespace melodick::app
