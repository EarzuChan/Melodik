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

float midi_to_hz(const double midi) {
    if (midi <= 0.0) {
        return 0.0f;
    }
    return 440.0f * std::pow(2.0f, static_cast<float>((midi - 69.0) / 12.0));
}

float sample_track_linear(const std::vector<float>& track, const double u) {
    if (track.empty()) {
        return 0.0f;
    }
    if (track.size() == 1) {
        return track.front();
    }

    const double pos = std::clamp(u, 0.0, 1.0) * static_cast<double>(track.size() - 1);
    const auto i0 = static_cast<std::size_t>(std::floor(pos));
    const auto i1 = std::min<std::size_t>(i0 + 1, track.size() - 1);
    const auto frac = static_cast<float>(pos - static_cast<double>(i0));
    return track[i0] * (1.0f - frac) + track[i1] * frac;
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

bool NoteBlob::has_voiced_content() const {
    for (const auto& point : original_pitch_curve) {
        if (point.voiced) {
            return true;
        }
    }
    for (const auto hz : source_f0_hz) {
        if (hz > 0.0f) {
            return true;
        }
    }
    return false;
}

bool NoteBlob::is_unvoiced_only() const {
    return !original_pitch_curve.empty() && !has_voiced_content();
}

bool NoteBlob::is_unedited() const {
    if (!nearly_equal(time.start_seconds, original_start_seconds)) {
        return false;
    }
    if (!nearly_equal(current_duration_seconds(), original_duration_seconds)) {
        return false;
    }
    if (!nearly_equal(global_pitch_delta_midi, 0.0)) {
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

float NoteBlob::sample_source_f0_hz(const double u) const {
    if (!source_f0_hz.empty()) {
        return std::max(0.0f, sample_track_linear(source_f0_hz, u));
    }

    if (original_pitch_curve.empty()) {
        return 0.0f;
    }
    if (original_pitch_curve.size() == 1) {
        const auto& point = original_pitch_curve.front();
        return point.voiced ? midi_to_hz(point.midi) : 0.0f;
    }

    const double pos = std::clamp(u, 0.0, 1.0) * static_cast<double>(original_pitch_curve.size() - 1);
    const auto i0 = static_cast<std::size_t>(std::floor(pos));
    const auto i1 = std::min<std::size_t>(i0 + 1, original_pitch_curve.size() - 1);
    const auto frac = static_cast<float>(pos - static_cast<double>(i0));
    const auto& p0 = original_pitch_curve[i0];
    const auto& p1 = original_pitch_curve[i1];
    if (!(p0.voiced && p1.voiced)) {
        return p0.voiced ? midi_to_hz(p0.midi) : 0.0f;
    }
    const auto midi = static_cast<float>(p0.midi) * (1.0f - frac) + static_cast<float>(p1.midi) * frac;
    return midi_to_hz(midi);
}

float NoteBlob::sample_source_voiced_probability(const double u) const {
    if (!source_voiced_probability.empty()) {
        return std::clamp(sample_track_linear(source_voiced_probability, u), 0.0f, 1.0f);
    }

    if (original_pitch_curve.empty()) {
        return 0.0f;
    }
    if (original_pitch_curve.size() == 1) {
        const auto& point = original_pitch_curve.front();
        return point.voiced ? std::clamp(point.confidence, 0.0f, 1.0f) : 0.0f;
    }

    const double pos = std::clamp(u, 0.0, 1.0) * static_cast<double>(original_pitch_curve.size() - 1);
    const auto i0 = static_cast<std::size_t>(std::floor(pos));
    const auto i1 = std::min<std::size_t>(i0 + 1, original_pitch_curve.size() - 1);
    const auto frac = static_cast<float>(pos - static_cast<double>(i0));
    const auto c0 = original_pitch_curve[i0].voiced ? std::clamp(original_pitch_curve[i0].confidence, 0.0f, 1.0f) : 0.0f;
    const auto c1 = original_pitch_curve[i1].voiced ? std::clamp(original_pitch_curve[i1].confidence, 0.0f, 1.0f) : 0.0f;
    return std::clamp(c0 * (1.0f - frac) + c1 * frac, 0.0f, 1.0f);
}

double NoteBlob::sample_pitch_delta_midi(const double u) const {
    const auto clamped_u = std::clamp(u, 0.0, 1.0);
    double delta = global_pitch_delta_midi;
    if (!handdraw_patch_midi.empty()) {
        const auto handdraw = sample_track_linear(handdraw_patch_midi, clamped_u);
        if (std::isfinite(handdraw)) {
            delta += static_cast<double>(handdraw);
        }
    }
    for (const auto& line : line_patches) {
        delta += line_patch_delta_midi(line, clamped_u);
    }
    return delta;
}
    
void NoteBlobOps::apply_pitch_delta(NoteBlob& note, double delta_midi) {
    note.global_pitch_delta_midi += delta_midi;
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

        double delta = global_pitch_delta_midi;
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
