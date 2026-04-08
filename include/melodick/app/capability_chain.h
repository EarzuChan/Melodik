#pragma once

#include <memory>
#include <vector>

#include "melodick/capabilities/backends.h"
#include "melodick/capabilities/segmenter.h"
#include "melodick/core/note_blob.h"

namespace melodick::app {

struct CapabilityAnalysis {
    std::vector<core::NoteBlob> blobs {};
};

class CapabilityChain {
public:
    CapabilityChain(
        std::shared_ptr<capabilities::IPitchExtractor> pitch_extractor,
        std::shared_ptr<capabilities::IVocoder> vocoder,
        const capabilities::SegmenterConfig& segmenter_config = {});

    [[nodiscard]] CapabilityAnalysis analyze_and_segment(const std::vector<float>& mono_samples, int sample_rate) const;
    void prepare_blob(core::NoteBlob& blob, int sample_rate) const;
    [[nodiscard]] std::vector<float> resynthesize_group(
        const std::vector<core::NoteBlob>& blobs,
        int sample_rate) const;

private:
    std::shared_ptr<capabilities::IPitchExtractor> pitch_extractor_;
    std::shared_ptr<capabilities::IVocoder> vocoder_;
    capabilities::NoteBlobSegmenter segmenter_;
};

} // namespace melodick::app
