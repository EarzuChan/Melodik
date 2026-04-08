#include "melodick/capabilities/segmenter.h"

#include <cmath>
#include <limits>
#include <numeric>

namespace melodick::capabilities {

NoteBlobSegmenter::NoteBlobSegmenter(SegmenterConfig config)
    : config_(config) {}

std::vector<core::NoteBlob> NoteBlobSegmenter::build_segments(const core::PitchSlice& f0) const {
    std::vector<core::NoteBlob> notes {};
    if (f0.empty()) {
        return notes;
    }

    std::size_t segment_start = 0;
    std::int64_t next_id = 1;

    auto flush_segment = [&](std::size_t start_index, std::size_t end_index) {
        if (end_index <= start_index || end_index >= f0.size()) {
            return;
        }

        const double start_time = f0[start_index].seconds;
        const double end_time = f0[end_index].seconds;
        if ((end_time - start_time) < config_.min_note_seconds) {
            return;
        }

        core::PitchSlice original {};
        for (std::size_t i = start_index; i <= end_index; ++i) {
            original.push_back(f0[i]);
        }

        core::NoteBlob note {};
        note.id = next_id++;
        note.time = {.start_seconds = start_time, .end_seconds = end_time};
        note.original_start_seconds = start_time;
        note.original_duration_seconds = end_time - start_time;
        note.original_pitch_curve = original;
        note.handdraw_patch_midi.assign(original.size(), std::numeric_limits<float>::quiet_NaN());
        note.global_transpose_semitones = 0.0;
        note.time_ratio = 1.0;
        notes.push_back(std::move(note));
    };

    for (std::size_t i = 1; i < f0.size(); ++i) {
        const auto& prev = f0[i - 1];
        const auto& curr = f0[i];

        const double dt = std::fabs(curr.seconds - prev.seconds);
        const bool unvoiced_cut = (!prev.voiced || !curr.voiced) && dt > config_.max_unvoiced_gap_seconds;
        const bool pitch_cut = prev.voiced && curr.voiced && std::fabs(curr.midi - prev.midi) >= config_.pitch_jump_semitones;

        if (unvoiced_cut || pitch_cut) {
            flush_segment(segment_start, i - 1);
            segment_start = i;
        }
    }

    flush_segment(segment_start, f0.size() - 1);

    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (i > 0) {
            notes[i].link_prev = notes[i - 1].id;
        }
        if (i + 1 < notes.size()) {
            notes[i].link_next = notes[i + 1].id;
        }
    }

    return notes;
}

} // namespace melodick::capabilities
