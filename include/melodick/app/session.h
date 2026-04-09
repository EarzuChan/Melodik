#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "melodick/app/capability_chain.h"
#include "melodick/project/project_state.h"
#include "melodick/render/dirty_timeline.h"
#include "melodick/render/lazy_render_scheduler.h"
#include "melodick/render/render_group_planner.h"

namespace melodick::app {

constexpr int kSessionSampleRate = 44100;

struct TrackMixState {
    bool mute {false};
    bool solo {false};
    double gain_db {0.0};
};

struct TrackInfo {
    std::int64_t id {0};
    std::string name {};
    TrackMixState mix {};
    double duration_seconds {0.0};
    std::size_t blob_count {0};
    bool has_dirty {false};
};

struct TrackRenderPlan {
    std::int64_t track_id {0};
    std::vector<render::RenderUnit> units {};
};

enum class ExportMode {
    Mixdown,
    Stems,
};

struct ExportFormat {
    int sample_rate {44100};
    int channels {1};
    int bits_per_sample {16};
    bool ieee_float {false};
};

struct ExportRequest {
    ExportMode mode {ExportMode::Mixdown};
    ExportFormat format {};
    std::string output_path {};
    std::string stems_directory {};
    bool stems_respect_mute_solo {false};
};

class Session {
public:
    Session(
        std::shared_ptr<capabilities::IPitchExtractor> pitch_extractor,
        std::shared_ptr<capabilities::IVocoder> vocoder,
        capabilities::SegmenterConfig segmenter_config = {},
        render::RenderGroupingConfig render_grouping = {});

    [[nodiscard]] std::int64_t create_track(const std::string& name = {});
    void remove_track(std::int64_t track_id);
    void clear();

    void import_audio_to_track(
        std::int64_t track_id,
        const std::vector<float>& interleaved_samples,
        int sample_rate,
        int channels);
    void insert_audio_to_track(
        std::int64_t track_id,
        const std::vector<float>& interleaved_samples,
        int sample_rate,
        int channels,
        double insert_time_seconds);
    [[nodiscard]] std::int64_t import_audio_as_new_track(
        const std::vector<float>& interleaved_samples,
        int sample_rate,
        int channels,
        const std::string& name = {});

    void set_track_mute(std::int64_t track_id, bool mute);
    void set_track_solo(std::int64_t track_id, bool solo);
    void set_track_gain_db(std::int64_t track_id, double gain_db);

    void shift_blob_pitch(std::int64_t track_id, std::int64_t blob_id, double semitones);
    void stretch_blob_time(std::int64_t track_id, std::int64_t blob_id, double new_duration_seconds);

    [[nodiscard]] std::vector<TrackRenderPlan> plan_render_from(double playhead_seconds, std::size_t budget_units_per_track) const;
    void render_units(const std::vector<TrackRenderPlan>& plans);
    void render_all_dirty(std::size_t budget_units_per_track = 32);

    [[nodiscard]] std::vector<float> build_mixdown() const;
    [[nodiscard]] std::vector<float> build_track_audio(std::int64_t track_id, bool apply_track_gain = true) const;
    void export_audio(const ExportRequest& request) const;

    [[nodiscard]] project::ProjectState capture_project_state() const;
    void restore_project_state(const project::ProjectState& state);

    [[nodiscard]] std::vector<TrackInfo> tracks() const;
    [[nodiscard]] const std::vector<core::NoteBlob>& track_blobs(std::int64_t track_id) const;
    [[nodiscard]] const render::DirtyTimeline& track_dirty_timeline(std::int64_t track_id) const;
    [[nodiscard]] int session_sample_rate() const { return kSessionSampleRate; }
    [[nodiscard]] double duration_seconds() const;

private:
    CapabilityChain chain_;
    render::RenderGroupPlanner planner_;
    render::LazyRenderScheduler scheduler_;

    struct RenderedBlobCache {
        std::vector<float> audio {};
        std::uint64_t source_revision {0};
    };

    struct TrackState {
        std::int64_t id {0};
        std::string name {};
        TrackMixState mix {};
        double duration_seconds {0.0};
        std::vector<core::NoteBlob> blobs {};
        std::vector<render::RenderUnit> render_units {};
        render::DirtyTimeline dirty_timeline {};
        std::unordered_map<std::int64_t, RenderedBlobCache> rendered_blob_cache {};
    };

    std::vector<TrackState> tracks_ {};
    std::int64_t next_track_id_ {1};

    [[nodiscard]] TrackState* find_track(std::int64_t track_id);
    [[nodiscard]] const TrackState* find_track(std::int64_t track_id) const;
    [[nodiscard]] core::NoteBlob* find_blob(TrackState& track, std::int64_t blob_id);
    [[nodiscard]] const core::NoteBlob* find_blob(const TrackState& track, std::int64_t blob_id) const;
    void refresh_derived_dirty(TrackState& track) const;
    [[nodiscard]] bool is_unit_dirty(const TrackState& track, const render::RenderUnit& unit) const;
    [[nodiscard]] bool is_track_active_in_mix(const TrackState& track) const;
};

} // namespace melodick::app
