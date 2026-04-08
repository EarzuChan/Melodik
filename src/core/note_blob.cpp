#include "melodick/core/note_blob.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace melodick::core {

namespace {

constexpr double kTol = 1.0e-6;

bool nearly_equal(double a, double b) {
    return std::fabs(a - b) <= kTol;
}

bool handdraw_is_effective(float v) {
    return std::isfinite(v) && std::fabs(static_cast<double>(v)) > kTol;
}

double line_patch_delta_midi(const LinePatch& line, double u) {
    const double start_u = std::clamp(line.start_u, 0.0, 1.0);
    const double end_u = std::clamp(line.end_u, 0.0, 1.0);
    if (end_u <= start_u) {
        return 0.0;
    }
    if (u < start_u || u > end_u) {
        return 0.0;
    }

    const double t = (u - start_u) / (end_u - start_u);
    switch (line.type) {
    case LinePatchType::Glide:
    case LinePatchType::Free: {
        return line.start_delta_midi + (line.end_delta_midi - line.start_delta_midi) * std::clamp(t, 0.0, 1.0);
    }
    case LinePatchType::Vibrato: {
        if (std::fabs(line.vibrato_depth_midi) <= kTol || std::fabs(line.vibrato_cycles) <= kTol) {
            return 0.0;
        }
        const double phase = std::clamp(t, 0.0, 1.0);
        return line.vibrato_depth_midi * std::sin(2.0 * std::numbers::pi_v<double> * line.vibrato_cycles * phase);
    }
    default:
        return 0.0;
    }
}

double point_u(const NoteBlob& note, const PitchPoint& p, std::size_t index, std::size_t count) {
    if (note.original_duration_seconds > kTol) {
        const double u = (p.seconds - note.original_start_seconds) / note.original_duration_seconds;
        if (std::isfinite(u)) {
            return std::clamp(u, 0.0, 1.0);
        }
    }
    if (count <= 1) {
        return 0.0;
    }
    return static_cast<double>(index) / static_cast<double>(count - 1);
}

} // namespace

void NoteBlobOps::touch(NoteBlob& note) {
    ++note.edit_revision;
}

bool NoteBlob::is_unedited() const {
    if (!nearly_equal(time.start_seconds, original_start_seconds)) {
        return false;
    }
    if (!nearly_equal(current_duration_seconds(), original_duration_seconds)) {
        return false;
    }
    if (!nearly_equal(global_transpose_semitones, 0.0)) {
        return false;
    }
    if (!nearly_equal(time_ratio, 1.0)) {
        return false;
    }
    if (!nearly_equal(loudness_gain_db, 0.0)) {
        return false;
    }
    if (!line_patches.empty()) {
        return false;
    }
    for (const auto v : handdraw_patch_midi) {
        if (handdraw_is_effective(v)) {
            return false;
        }
    }
    return true;
}

void NoteBlobOps::shift_pitch(NoteBlob& note, double semitones) {
    note.global_transpose_semitones += semitones;
    touch(note);
}

void NoteBlobOps::stretch_time(NoteBlob& note, double new_duration_seconds) {
    if (new_duration_seconds <= 0.0) {
        throw std::invalid_argument("new_duration_seconds must be positive");
    }

    const double old_duration = note.original_duration_seconds;
    if (old_duration <= kTol) {
        throw std::invalid_argument("cannot stretch zero-length note");
    }

    note.time_ratio = new_duration_seconds / old_duration;
    note.time.end_seconds = note.time.start_seconds + new_duration_seconds;
    touch(note);
}

void NoteBlobOps::move_start(NoteBlob& note, double new_start_seconds) {
    if (!std::isfinite(new_start_seconds)) {
        throw std::invalid_argument("new_start_seconds must be finite");
    }
    note.time.start_seconds = new_start_seconds;
    note.time.end_seconds = new_start_seconds + note.current_duration_seconds();
    touch(note);
}

double NoteBlob::current_duration_seconds() const {
    if (original_duration_seconds > kTol && std::isfinite(time_ratio) && time_ratio > 0.0) {
        return original_duration_seconds * time_ratio;
    }
    return std::max(0.0, time.length());
}

PitchSlice NoteBlob::final_pitch_curve() const {
    PitchSlice out {};
    if (original_pitch_curve.empty()) {
        return out;
    }

    const double duration = current_duration_seconds();
    out.reserve(original_pitch_curve.size());

    for (std::size_t i = 0; i < original_pitch_curve.size(); ++i) {
        const auto& src = original_pitch_curve[i];
        const double u = point_u(*this, src, i, original_pitch_curve.size());

        double delta = global_transpose_semitones;
        if (i < handdraw_patch_midi.size() && std::isfinite(handdraw_patch_midi[i])) {
            delta += static_cast<double>(handdraw_patch_midi[i]);
        }
        for (const auto& line : line_patches) {
            delta += line_patch_delta_midi(line, u);
        }

        PitchPoint p {};
        p.seconds = time.start_seconds + duration * u;
        p.voiced = src.voiced;
        p.confidence = src.confidence;
        p.midi = p.voiced ? (src.midi + delta) : 0.0;
        out.push_back(p);
    }
    return out;
}

double NoteBlob::final_display_pitch_midi() const {
    const auto final = final_pitch_curve();
    double sum = 0.0;
    std::size_t count = 0;
    for (const auto& p : final) {
        if (!p.voiced) {
            continue;
        }
        sum += p.midi;
        ++count;
    }
    if (count == 0) {
        return 0.0;
    }
    return sum / static_cast<double>(count);
}

} // namespace melodick::core
