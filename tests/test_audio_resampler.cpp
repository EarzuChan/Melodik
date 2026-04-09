#include "test_framework.h"

#include <cmath>
#include <numbers>
#include <vector>

#include "melodick/core/audio_resampler.h"
#include "melodick/core/pitch_preprocessing.h"

namespace {

double rms(const std::vector<float>& samples) {
    if (samples.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const auto sample : samples) {
        const double value = static_cast<double>(sample);
        sum += value * value;
    }
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

std::vector<float> sine_wave(const int sample_rate, const double frequency_hz, const double amplitude, const std::size_t samples) {
    std::vector<float> out(samples, 0.0f);
    for (std::size_t i = 0; i < samples; ++i) {
        const double phase = 2.0 * std::numbers::pi_v<double> * frequency_hz * static_cast<double>(i) / static_cast<double>(sample_rate);
        out[i] = static_cast<float>(amplitude * std::sin(phase));
    }
    return out;
}

} // namespace

MELODICK_TEST(audio_resampler_identity_preserves_signal) {
    const std::vector<float> input {0.0f, 0.25f, -0.5f, 0.75f, -1.0f, 0.5f};
    const auto output = melodick::core::resample_audio_to_size(input, input.size());

    MELODICK_EXPECT_EQ(output.size(), input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        MELODICK_EXPECT_TRUE(std::fabs(static_cast<double>(output[i] - input[i])) < 1.0e-6);
    }
}

MELODICK_TEST(audio_resampler_rate_preserves_constant_signal) {
    const std::vector<float> input(4096, 0.125f);
    const auto down = melodick::core::resample_audio_rate(input, 44100, 16000);
    const auto up = melodick::core::resample_audio_rate(down, 16000, 44100);

    MELODICK_EXPECT_TRUE(!down.empty());
    MELODICK_EXPECT_TRUE(!up.empty());
    for (const auto sample : down) {
        MELODICK_EXPECT_TRUE(std::fabs(static_cast<double>(sample) - 0.125) < 2.0e-3);
    }
    for (const auto sample : up) {
        MELODICK_EXPECT_TRUE(std::fabs(static_cast<double>(sample) - 0.125) < 3.0e-3);
    }
}

MELODICK_TEST(audio_resampler_to_size_keeps_impulse_finite_and_bounded) {
    std::vector<float> input(128, 0.0f);
    input[64] = 1.0f;

    const auto output = melodick::core::resample_audio_to_size(input, 257);
    MELODICK_EXPECT_EQ(output.size(), static_cast<std::size_t>(257));

    bool found_energy = false;
    for (const auto sample : output) {
        MELODICK_EXPECT_TRUE(std::isfinite(sample));
        MELODICK_EXPECT_TRUE(std::fabs(static_cast<double>(sample)) <= 1.05);
        found_energy = found_energy || std::fabs(static_cast<double>(sample)) > 1.0e-4;
    }
    MELODICK_EXPECT_TRUE(found_energy);
}

MELODICK_TEST(rmvpe_preprocess_disabled_is_noop) {
    std::vector<float> input {0.0f, 0.2f, -0.1f, 0.3f, -0.4f, 0.5f};
    const auto original = input;

    melodick::core::preprocess_rmvpe_audio_in_place(
        input,
        16000,
        melodick::core::RmvpeAudioPreprocessConfig {
            .enabled = false,
            .highpass_hz = 50.0f,
            .noise_gate_dbfs = -50.0f,
        });

    MELODICK_EXPECT_EQ(input.size(), original.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        MELODICK_EXPECT_TRUE(std::fabs(static_cast<double>(input[i] - original[i])) < 1.0e-9);
    }
}

MELODICK_TEST(rmvpe_preprocess_rejects_sub_threshold_noise) {
    std::vector<float> input(1024, 0.001f);

    melodick::core::preprocess_rmvpe_audio_in_place(
        input,
        16000,
        melodick::core::RmvpeAudioPreprocessConfig {
            .enabled = true,
            .highpass_hz = 50.0f,
            .noise_gate_dbfs = -50.0f,
        });

    for (const auto sample : input) {
        MELODICK_EXPECT_TRUE(sample == 0.0f);
    }
}

MELODICK_TEST(rmvpe_preprocess_attenuates_dc_and_preserves_voiced_band) {
    constexpr int kSampleRate = 16000;
    auto low = sine_wave(kSampleRate, 10.0, 0.1, 4096);
    auto voiced = sine_wave(kSampleRate, 220.0, 0.1, 4096);

    melodick::core::preprocess_rmvpe_audio_in_place(
        low,
        kSampleRate,
        melodick::core::RmvpeAudioPreprocessConfig {
            .enabled = true,
            .highpass_hz = 50.0f,
            .noise_gate_dbfs = -80.0f,
        });
    melodick::core::preprocess_rmvpe_audio_in_place(
        voiced,
        kSampleRate,
        melodick::core::RmvpeAudioPreprocessConfig {
            .enabled = true,
            .highpass_hz = 50.0f,
            .noise_gate_dbfs = -80.0f,
        });

    MELODICK_EXPECT_TRUE(rms(low) < 0.01);
    MELODICK_EXPECT_TRUE(rms(voiced) > 0.04);
}
