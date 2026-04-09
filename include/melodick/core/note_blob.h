#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#include "melodick/core/pitch_data.h"
#include "melodick/core/time_range.h"

namespace melodick::core {

enum class LinePatchType : std::int32_t {
    Glide = 0,
    Vibrato = 1,
    Free = 2,
};

struct LinePatch {
    LinePatchType type {LinePatchType::Glide};
    double start_u {0.0};
    double end_u {1.0};
    double start_delta_midi {0.0};
    double end_delta_midi {0.0};
    double vibrato_depth_midi {0.0};
    double vibrato_cycles {0.0};
};

struct NoteBlob {
    std::int64_t id {0};
    TimeRange time;

    double original_start_seconds {0.0};
    double original_duration_seconds {0.0};

    PitchSlice original_pitch_curve {};
    std::vector<float> source_f0_hz {};
    std::vector<float> source_voiced_probability {};
    std::vector<float> handdraw_patch_midi {}; // 手绘Patch，NaN代表该点无手绘
    std::vector<LinePatch> line_patches {}; // 多个线条Patch实例
    std::vector<float> source_audio_44k {};
    std::vector<float> cached_source_mel_log {};
    int cached_source_mel_bins {0};
    int cached_source_mel_frames {0};

    double global_pitch_delta_midi {0.0}; // 整数是半音
    double time_ratio {1.0};
    double loudness_gain_db {0.0};

    std::optional<std::int64_t> link_prev {};
    std::optional<std::int64_t> link_next {};
    bool detached {false};
    std::uint64_t edit_revision {1};

    [[nodiscard]] double original_end_seconds() const { return original_start_seconds + original_duration_seconds; }
    [[nodiscard]] double current_duration_seconds() const;
    [[nodiscard]] double duration() const { return time.length(); }
    [[nodiscard]] PitchSlice final_pitch_curve() const;
    [[nodiscard]] double final_display_pitch_midi() const;
    [[nodiscard]] float sample_source_f0_hz(double u) const;
    [[nodiscard]] float sample_source_voiced_probability(double u) const;
    [[nodiscard]] double sample_pitch_delta_midi(double u) const;
    [[nodiscard]] bool has_voiced_content() const;
    [[nodiscard]] bool is_unvoiced_only() const;
    [[nodiscard]] bool is_unedited() const;
};

class NoteBlobOps {
public:
    static void apply_pitch_delta(NoteBlob& note, double delta_midi);
    static void stretch_time(NoteBlob& note, double new_duration_seconds);
    static void move_start(NoteBlob& note, double new_start_seconds);
    static void touch(NoteBlob& note);
};

} // namespace melodick::core
