#pragma once

#include <vector>

namespace melodick::core {

struct RmvpeAudioPreprocessConfig {
    bool enabled {false};
    float highpass_hz {50.0f};
    float noise_gate_dbfs {-50.0f};
};

void preprocess_rmvpe_audio_in_place(
    std::vector<float>& mono_samples,
    int sample_rate,
    const RmvpeAudioPreprocessConfig& config);

} // namespace melodick::core
