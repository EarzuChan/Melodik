#pragma once

#include <memory>
#include <vector>

#include "melodick/capabilities/backends.h"
#include "melodick/capabilities/segmenter.h"
#include "melodick/core/note_blob.h"

namespace melodick::app {

struct CapabilityAnalysis {
    core::PitchSlice f0 {};
    std::vector<core::NoteBlob> blobs {};
};

class CapabilityChain {
public:
    CapabilityChain(
        std::shared_ptr<capabilities::IPitchExtractor> pitch_extractor,
        std::shared_ptr<capabilities::IVocoder> vocoder,
        const capabilities::SegmenterConfig& segmenter_config = {});

    [[nodiscard]] CapabilityAnalysis analyze_and_segment(const std::vector<float>& mono_samples, int sample_rate) const;
    [[nodiscard]] std::vector<float> resynthesize_blob(
        const core::NoteBlob& blob,
        const std::vector<float>& source_audio,
        int sample_rate) const;

private:
    std::shared_ptr<capabilities::IPitchExtractor> pitch_extractor_;
    std::shared_ptr<capabilities::IVocoder> vocoder_;
    capabilities::NoteBlobSegmenter segmenter_;
};

} // namespace melodick::app
