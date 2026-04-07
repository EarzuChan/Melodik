#pragma once

#include <string>
#include <vector>

namespace melodick::io {

struct WavData {
    int sample_rate {0};
    std::vector<float> mono_samples {};
};

WavData read_wav_mono(const std::string& path);
void write_wav_mono_16(const std::string& path, const std::vector<float>& mono_samples, int sample_rate);

} // namespace melodick::io
