#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace melodick::io {

struct WavData {
    int sample_rate {0};
    int channels {0};
    int bits_per_sample {0};
    bool is_float {false};
    std::vector<float> interleaved_samples {};
};

struct WavWriteSpec {
    int sample_rate {44100};
    int channels {1};
    int bits_per_sample {16};
    bool ieee_float {false};
};

WavData read_wav(const std::string& path);
std::vector<float> downmix_to_mono(const std::vector<float>& interleaved_samples, int channels);
void write_wav(const std::string& path, const std::vector<float>& interleaved_samples, const WavWriteSpec& spec);
void write_wav_mono_16(const std::string& path, const std::vector<float>& mono_samples, int sample_rate);

} // namespace melodick::io
