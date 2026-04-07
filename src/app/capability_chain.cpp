#include "melodick/app/capability_chain.h"

#include <stdexcept>

namespace melodick::app {

CapabilityChain::CapabilityChain(
    std::shared_ptr<capabilities::IPitchExtractor> pitch_extractor,
    std::shared_ptr<capabilities::IVocoder> vocoder,
    const capabilities::SegmenterConfig& segmenter_config)
    : pitch_extractor_(std::move(pitch_extractor))
    , vocoder_(std::move(vocoder))
    , segmenter_(segmenter_config) {
    if (!pitch_extractor_ || !vocoder_) {
        throw std::invalid_argument("capability chain requires pitch extractor and vocoder");
    }
}

CapabilityAnalysis CapabilityChain::analyze_and_segment(const std::vector<float>& mono_samples, int sample_rate) const {
    CapabilityAnalysis out {};
    out.f0 = pitch_extractor_->extract_f0(mono_samples, sample_rate);
    out.blobs = segmenter_.build_segments(out.f0);
    return out;
}

std::vector<float> CapabilityChain::resynthesize_blob(
    const core::NoteBlob& blob,
    const std::vector<float>& source_audio,
    const int sample_rate) const {
    return vocoder_->render_note_audio(blob, source_audio, sample_rate);
}

} // namespace melodick::app
