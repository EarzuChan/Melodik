#pragma once

#include <vector>

#include "melodick/core/note_blob.h"

namespace melodick::capabilities {

struct SegmenterConfig {
    double min_note_seconds {0.08};
    double max_unvoiced_gap_seconds {0.03};
    double pitch_jump_semitones {2.8};
};

class NoteBlobSegmenter {
public:
    explicit NoteBlobSegmenter(SegmenterConfig config = {});
    [[nodiscard]] std::vector<core::NoteBlob> build_segments(const core::PitchSlice& f0) const;

private:
    SegmenterConfig config_;
};

} // namespace melodick::capabilities
