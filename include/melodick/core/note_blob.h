#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>

#include "melodick/core/pitch_data.h"
#include "melodick/core/time_range.h"

namespace melodick::core {

struct NoteBlob {
    std::int64_t id {0};
    TimeRange time;

    double original_start_seconds {0.0};
    double original_end_seconds {0.0};

    PitchSlice original_pitch_slice {};
    PitchSlice edited_pitch_slice {};

    double display_pitch_midi {0.0};
    double pitch_offset_semitones {0.0};
    double time_stretch_ratio {1.0};
    double loudness_gain_db {0.0};

    std::optional<std::int64_t> link_prev {};
    std::optional<std::int64_t> link_next {};
    bool detached {false};
    std::uint64_t edit_revision {1};

    [[nodiscard]] double duration() const { return time.length(); }
    [[nodiscard]] bool is_unedited() const;
};

class NoteBlobOps {
public:
    static void shift_pitch(NoteBlob& note, double semitones);
    static void stretch_time(NoteBlob& note, double new_duration_seconds);
    static void touch(NoteBlob& note);
};

} // namespace melodick::core
