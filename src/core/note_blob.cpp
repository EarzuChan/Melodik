#include "melodick/core/note_blob.h"

#include <cmath>

namespace melodick::core {

namespace {

constexpr double kTol = 1.0e-6;

bool nearly_equal(double a, double b) {
    return std::fabs(a - b) <= kTol;
}

} // namespace

void NoteBlobOps::touch(NoteBlob& note) {
    ++note.edit_revision;
}

bool NoteBlob::is_unedited() const {
    if (!nearly_equal(time.start_seconds, original_start_seconds)) {
        return false;
    }
    if (!nearly_equal(time.end_seconds, original_end_seconds)) {
        return false;
    }
    if (!nearly_equal(pitch_offset_semitones, 0.0)) {
        return false;
    }
    if (!nearly_equal(time_stretch_ratio, 1.0)) {
        return false;
    }
    if (!nearly_equal(loudness_gain_db, 0.0)) {
        return false;
    }
    if (edited_pitch_slice.size() != original_pitch_slice.size()) {
        return false;
    }
    for (std::size_t i = 0; i < edited_pitch_slice.size(); ++i) {
        const auto& a = edited_pitch_slice[i];
        const auto& b = original_pitch_slice[i];
        if (a.voiced != b.voiced) {
            return false;
        }
        if (!nearly_equal(a.seconds, b.seconds)) {
            return false;
        }
        if (!nearly_equal(a.midi, b.midi)) {
            return false;
        }
    }
    return true;
}

void NoteBlobOps::shift_pitch(NoteBlob& note, double semitones) {
    note.pitch_offset_semitones += semitones;
    note.display_pitch_midi += semitones;

    for (auto& point : note.edited_pitch_slice) {
        point.midi += semitones;
    }
    touch(note);
}

void NoteBlobOps::stretch_time(NoteBlob& note, double new_duration_seconds) {
    if (new_duration_seconds <= 0.0) {
        throw std::invalid_argument("new_duration_seconds must be positive");
    }

    const double old_duration = note.duration();
    if (old_duration <= 0.0) {
        throw std::invalid_argument("cannot stretch zero-length note");
    }

    const double ratio = new_duration_seconds / old_duration;
    note.time_stretch_ratio *= ratio;
    note.time.end_seconds = note.time.start_seconds + new_duration_seconds;

    for (auto& point : note.edited_pitch_slice) {
        const double relative = point.seconds - note.time.start_seconds;
        point.seconds = note.time.start_seconds + (relative * ratio);
    }
    touch(note);
}

} // namespace melodick::core
