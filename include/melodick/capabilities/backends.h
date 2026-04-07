#pragma once

#include <memory>
#include <string>
#include <vector>

#include "melodick/core/note_blob.h"
#include "melodick/core/pitch_data.h"

namespace melodick::capabilities {

struct BackendConfig {
    std::string rmvpe_model_path {};
    std::string hifigan_model_path {};
    int inference_threads {1};
    float uv_threshold {0.5f};
    bool enable_uv_check {false};
};

class IPitchExtractor {
public:
    virtual ~IPitchExtractor() = default;
    virtual core::PitchSlice extract_f0(const std::vector<float>& mono_samples, int sample_rate) = 0;
};

class IVocoder {
public:
    virtual ~IVocoder() = default;
    virtual std::vector<float> render_note_audio(
        const core::NoteBlob& note,
        const std::vector<float>& source_audio,
        int sample_rate) = 0;
};

std::shared_ptr<IPitchExtractor> create_pitch_extractor(const BackendConfig& config);
std::shared_ptr<IVocoder> create_vocoder(const BackendConfig& config);

BackendConfig default_backend_config();

} // namespace melodick::capabilities
