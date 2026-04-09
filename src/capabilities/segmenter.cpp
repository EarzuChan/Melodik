#include "melodick/capabilities/segmenter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace {

struct FrameSpan {
    std::size_t start_index {0};
    std::size_t end_index {0};
    bool voiced {false};
};

float midi_to_hz(const double midi) {
    if (midi <= 0.0) {
        return 0.0f;
    }
    return 440.0f * std::pow(2.0f, static_cast<float>((midi - 69.0) / 12.0));
}

double midpoint_seconds(const double a, const double b) {
    return a + ((b - a) * 0.5);
}

double frame_start_seconds(const melodick::core::PitchSlice& f0, const std::size_t index) {
    if (f0.empty() || index >= f0.size()) {
        return 0.0;
    }
    if (index == 0) {
        if (f0.size() == 1) {
            return std::max(0.0, f0.front().seconds);
        }
        const double delta = std::max(0.0, f0[1].seconds - f0[0].seconds);
        return std::max(0.0, f0[0].seconds - (delta * 0.5));
    }
    return midpoint_seconds(f0[index - 1].seconds, f0[index].seconds);
}

double frame_end_seconds(const melodick::core::PitchSlice& f0, const std::size_t index) {
    if (f0.empty() || index >= f0.size()) {
        return 0.0;
    }
    if (index + 1 < f0.size()) {
        return midpoint_seconds(f0[index].seconds, f0[index + 1].seconds);
    }
    if (index == 0) {
        return std::max(0.0, f0.front().seconds);
    }
    const double delta = std::max(0.0, f0[index].seconds - f0[index - 1].seconds);
    return f0[index].seconds + (delta * 0.5);
}

double span_duration_seconds(const melodick::core::PitchSlice& f0, const FrameSpan& span) {
    if (span.start_index > span.end_index || span.end_index >= f0.size()) {
        return 0.0;
    }
    return std::max(0.0, frame_end_seconds(f0, span.end_index) - frame_start_seconds(f0, span.start_index));
}

void prepend_points_to_note(
    melodick::core::NoteBlob& note,
    const melodick::core::PitchSlice& prefix,
    const double new_start_time) {
    if (prefix.empty()) {
        return;
    }

    std::vector<float> prefix_f0 {};
    std::vector<float> prefix_uv {};
    prefix_f0.reserve(prefix.size());
    prefix_uv.reserve(prefix.size());
    for (const auto& point : prefix) {
        prefix_f0.push_back(point.voiced ? midi_to_hz(point.midi) : 0.0f);
        prefix_uv.push_back(point.voiced ? std::clamp(point.confidence, 0.0f, 1.0f) : 0.0f);
    }

    note.original_pitch_curve.insert(note.original_pitch_curve.begin(), prefix.begin(), prefix.end());
    note.source_f0_hz.insert(note.source_f0_hz.begin(), prefix_f0.begin(), prefix_f0.end());
    note.source_voiced_probability.insert(note.source_voiced_probability.begin(), prefix_uv.begin(), prefix_uv.end());
    note.handdraw_patch_midi.insert(note.handdraw_patch_midi.begin(), prefix.size(), std::numeric_limits<float>::quiet_NaN());

    note.original_duration_seconds += (note.original_start_seconds - new_start_time);
    note.original_start_seconds = new_start_time;
    note.time.start_seconds = new_start_time;
}

void absorb_short_unvoiced_gaps_into_following_voiced_notes(
    std::vector<melodick::core::NoteBlob>& notes,
    const melodick::core::PitchSlice& f0,
    const melodick::capabilities::SegmenterConfig& config) {
    if (notes.size() < 2 || f0.empty()) {
        return;
    }

    for (std::size_t note_index = 1; note_index < notes.size(); ++note_index) {
        auto& note = notes[note_index];
        if (!note.has_voiced_content()) {
            continue;
        }

        const double gap_start = notes[note_index - 1].time.end_seconds;
        const double gap_end = note.time.start_seconds;
        const double gap_duration = gap_end - gap_start;
        if (gap_duration <= 1.0e-9 || gap_duration > config.max_leading_unvoiced_seconds) {
            continue;
        }

        melodick::core::PitchSlice prefix {};
        for (std::size_t frame_index = 0; frame_index < f0.size(); ++frame_index) {
            const double frame_start = frame_start_seconds(f0, frame_index);
            const double frame_end = frame_end_seconds(f0, frame_index);
            if (frame_end <= gap_start || frame_start >= gap_end) {
                continue;
            }
            prefix.push_back(f0[frame_index]);
        }
        if (prefix.empty()) {
            continue;
        }

        const bool all_unvoiced = std::all_of(prefix.begin(), prefix.end(), [](const auto& point) {
            return !point.voiced;
        });
        if (!all_unvoiced) {
            continue;
        }

        prepend_points_to_note(note, prefix, gap_start);
    }
}

void donate_long_unvoiced_edges_to_neighboring_spans(
    std::vector<FrameSpan>& spans,
    const melodick::core::PitchSlice& f0,
    const melodick::capabilities::SegmenterConfig& config) {
    if (spans.empty() || f0.empty()) {
        return;
    }

    for (std::size_t i = 0; i < spans.size(); ++i) {
        auto& span = spans[i];
        if (span.voiced || span_duration_seconds(f0, span) <= config.max_leading_unvoiced_seconds) {
            continue;
        }

        const bool has_prev_voiced = i > 0 && spans[i - 1].voiced;
        const bool has_next_voiced = i + 1 < spans.size() && spans[i + 1].voiced;
        if (!has_prev_voiced && !has_next_voiced) {
            continue;
        }

        const double old_start = frame_start_seconds(f0, span.start_index);
        const double old_end = frame_end_seconds(f0, span.end_index);
        const double prefix_end = has_prev_voiced
            ? std::min(old_end, old_start + config.long_unvoiced_prefix_to_prev_seconds)
            : old_start;
        const double suffix_start = has_next_voiced
            ? std::max(prefix_end, old_end - config.long_unvoiced_suffix_to_next_seconds)
            : old_end;

        std::size_t middle_start = span.start_index;
        if (has_prev_voiced) {
            for (std::size_t frame_index = span.start_index; frame_index <= span.end_index; ++frame_index) {
                if (frame_end_seconds(f0, frame_index) <= (prefix_end + 1.0e-9)) {
                    spans[i - 1].end_index = frame_index;
                    middle_start = frame_index + 1;
                    continue;
                }
                break;
            }
        }

        std::size_t middle_end = span.end_index;
        if (has_next_voiced) {
            for (std::size_t frame_index = middle_start; frame_index <= span.end_index; ++frame_index) {
                if (frame_start_seconds(f0, frame_index) >= (suffix_start - 1.0e-9)) {
                    spans[i + 1].start_index = frame_index;
                    if (frame_index == 0) {
                        middle_end = 0;
                    } else {
                        middle_end = frame_index - 1;
                    }
                    break;
                }
            }
        }

        if (middle_start > middle_end || suffix_start <= prefix_end + 1.0e-9) {
            span.start_index = span.end_index + 1;
            continue;
        }
        span.start_index = middle_start;
        span.end_index = middle_end;
    }

    spans.erase(std::remove_if(spans.begin(), spans.end(), [](const auto& span) {
        return span.start_index > span.end_index;
    }), spans.end());
}

} // namespace

namespace melodick::capabilities {

NoteBlobSegmenter::NoteBlobSegmenter(SegmenterConfig config)
    : config_(config) {}

std::vector<core::NoteBlob> NoteBlobSegmenter::build_segments(const core::PitchSlice& f0) const {
    std::vector<core::NoteBlob> notes {};
    if (f0.empty()) {
        return notes;
    }

    std::int64_t next_id = 1;

    auto append_blob = [&](const FrameSpan& span) {
        if (span.start_index > span.end_index || span.end_index >= f0.size()) {
            return;
        }

        const double start_time = frame_start_seconds(f0, span.start_index);
        const double end_time = frame_end_seconds(f0, span.end_index);
        if ((end_time - start_time) <= 1.0e-9) {
            return;
        }

        core::PitchSlice original {};
        original.reserve(span.end_index - span.start_index + 1);
        for (std::size_t i = span.start_index; i <= span.end_index; ++i) {
            original.push_back(f0[i]);
        }

        core::NoteBlob note {};
        note.id = next_id++;
        note.time = {.start_seconds = start_time, .end_seconds = end_time};
        note.original_start_seconds = start_time;
        note.original_duration_seconds = end_time - start_time;
        note.original_pitch_curve = original;
        note.source_f0_hz.reserve(original.size());
        note.source_voiced_probability.reserve(original.size());
        for (const auto& point : original) {
            note.source_f0_hz.push_back(point.voiced ? midi_to_hz(point.midi) : 0.0f);
            note.source_voiced_probability.push_back(point.voiced ? std::clamp(point.confidence, 0.0f, 1.0f) : 0.0f);
        }
        note.handdraw_patch_midi.assign(original.size(), std::numeric_limits<float>::quiet_NaN());
        note.global_transpose_semitones = 0.0;
        note.time_ratio = 1.0;
        // TODO(after MVP): split render span and pitched core span so GUI can show partial-unvoiced
        // note heads like Melodyne instead of treating short lead-ins as part of the visible note body.
        notes.push_back(std::move(note));
    };

    std::vector<FrameSpan> raw_spans {};
    raw_spans.reserve(f0.size());
    std::size_t run_start = 0;
    for (std::size_t i = 1; i < f0.size(); ++i) {
        if (f0[i].voiced == f0[i - 1].voiced) {
            continue;
        }
        raw_spans.push_back(FrameSpan {
            .start_index = run_start,
            .end_index = i - 1,
            .voiced = f0[i - 1].voiced,
        });
        run_start = i;
    }
    raw_spans.push_back(FrameSpan {
        .start_index = run_start,
        .end_index = f0.size() - 1,
        .voiced = f0.back().voiced,
    });

    std::vector<FrameSpan> spans {};
    spans.reserve(raw_spans.size() * 2);
    for (const auto& run : raw_spans) {
        if (!run.voiced) {
            spans.push_back(run);
            continue;
        }

        std::size_t voiced_start = run.start_index;
        for (std::size_t i = run.start_index + 1; i <= run.end_index; ++i) {
            const auto& prev = f0[i - 1];
            const auto& curr = f0[i];
            const bool pitch_cut = std::fabs(curr.midi - prev.midi) >= config_.pitch_jump_semitones;
            if (!pitch_cut) {
                continue;
            }

            spans.push_back(FrameSpan {
                .start_index = voiced_start,
                .end_index = i - 1,
                .voiced = true,
            });
            voiced_start = i;
        }
        spans.push_back(FrameSpan {
            .start_index = voiced_start,
            .end_index = run.end_index,
            .voiced = true,
        });
    }

    std::vector<FrameSpan> merged_spans {};
    merged_spans.reserve(spans.size());
    for (std::size_t i = 0; i < spans.size(); ++i) {
        auto span = spans[i];
        if (!span.voiced
            && i + 1 < spans.size()
            && spans[i + 1].voiced
            && span_duration_seconds(f0, span) <= config_.max_leading_unvoiced_seconds) {
            spans[i + 1].start_index = span.start_index;
            continue;
        }
        merged_spans.push_back(span);
    }

    std::vector<FrameSpan> compact_spans {};
    compact_spans.reserve(merged_spans.size());
    for (std::size_t i = 0; i < merged_spans.size(); ++i) {
        auto span = merged_spans[i];
        if (span_duration_seconds(f0, span) >= config_.min_note_seconds) {
            compact_spans.push_back(span);
            continue;
        }

        if (i + 1 < merged_spans.size()) {
            merged_spans[i + 1].start_index = std::min(merged_spans[i + 1].start_index, span.start_index);
            continue;
        }
        if (!compact_spans.empty()) {
            compact_spans.back().end_index = std::max(compact_spans.back().end_index, span.end_index);
            continue;
        }

        compact_spans.push_back(span);
    }

    // Order matters: short unvoiced lead-ins are merged into the following voiced span first,
    // then longer unvoiced spans donate edge context (40ms to prev, 80ms to next).
    donate_long_unvoiced_edges_to_neighboring_spans(compact_spans, f0, config_);

    for (const auto& span : compact_spans) {
        append_blob(span);
    }

    absorb_short_unvoiced_gaps_into_following_voiced_notes(notes, f0, config_);

    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (i > 0 && notes[i - 1].has_voiced_content() && notes[i].has_voiced_content()) {
            notes[i].link_prev = notes[i - 1].id;
        }
        if (i + 1 < notes.size() && notes[i].has_voiced_content() && notes[i + 1].has_voiced_content()) {
            notes[i].link_next = notes[i + 1].id;
        }
    }

    return notes;
}

} // namespace melodick::capabilities
