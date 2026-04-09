#pragma once

#include <vector>

#include "melodick/core/note_blob.h"

namespace melodick::capabilities {

struct SegmenterConfig {
    double min_note_seconds {0.08};
    double max_leading_unvoiced_seconds {0.12}; // 合并前辅音音头
    double long_unvoiced_prefix_to_prev_seconds {0.03}; // 音尾送返
    double long_unvoiced_suffix_to_next_seconds {0.06}; // 音头送去
    double pitch_split_threshold_midi {2.8};
};

class NoteBlobSegmenter {
public:
    explicit NoteBlobSegmenter(const SegmenterConfig& config = {});
    [[nodiscard]] std::vector<core::NoteBlob> build_segments(const core::PitchSlice& f0) const;

private:
    SegmenterConfig config_;
};

} // namespace melodick::capabilities
