#include "test_framework.h"

#include <cmath>
#include <vector>

#include "melodick/core/audio_resampler.h"

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
