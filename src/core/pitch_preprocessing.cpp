#include "melodick/core/pitch_preprocessing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace melodick::core {

namespace {

struct Biquad {
    double b0 {1.0};
    double b1 {0.0};
    double b2 {0.0};
    double a1 {0.0};
    double a2 {0.0};
    double z1 {0.0};
    double z2 {0.0};

    float process(const float sample) {
        const double x = sample;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
};

Biquad make_highpass_biquad(const int sample_rate, const double cutoff_hz, const double q) {
    const double clamped_cutoff = std::clamp(cutoff_hz, 1.0, 0.49 * static_cast<double>(sample_rate));
    const double omega = 2.0 * std::numbers::pi_v<double> * clamped_cutoff / static_cast<double>(sample_rate);
    const double sin_omega = std::sin(omega);
    const double cos_omega = std::cos(omega);
    const double alpha = sin_omega / (2.0 * q);

    const double a0 = 1.0 + alpha;
    Biquad biquad {};
    biquad.b0 = (1.0 + cos_omega) * 0.5 / a0;
    biquad.b1 = -(1.0 + cos_omega) / a0;
    biquad.b2 = (1.0 + cos_omega) * 0.5 / a0;
    biquad.a1 = -2.0 * cos_omega / a0;
    biquad.a2 = (1.0 - alpha) / a0;
    return biquad;
}

void apply_highpass_butterworth_8(std::vector<float>& mono_samples, const int sample_rate, const double cutoff_hz) {
    if (mono_samples.empty()) {
        return;
    }

    std::array<Biquad, 4> stages {};
    for (std::size_t i = 0; i < stages.size(); ++i) {
        constexpr std::array kSectionQ {
            0.5097955791041592,
            0.6013448869350453,
            0.8999762231364156,
            2.5629154477415055,
        };
        stages[i] = make_highpass_biquad(sample_rate, cutoff_hz, kSectionQ[i]);
    }

    for (auto& sample : mono_samples) {
        float y = sample;
        for (auto& stage : stages) y = stage.process(y);
        sample = y;
    }
}

void apply_noise_gate(std::vector<float>& mono_samples, const double gate_dbfs) {
    const double threshold = std::pow(10.0, gate_dbfs / 20.0);
    for (auto& sample : mono_samples) if (std::fabs(static_cast<double>(sample)) < threshold) sample = 0.0f;
}

} // namespace

void preprocess_rmvpe_audio_in_place(
    std::vector<float>& mono_samples,
    const int sample_rate,
    const RmvpeAudioPreprocessConfig& config) {
    if (!config.enabled || mono_samples.empty()) return;
    if (sample_rate <= 0) throw std::invalid_argument("sample_rate must be positive");

    apply_highpass_butterworth_8(mono_samples, sample_rate, config.highpass_hz);
    apply_noise_gate(mono_samples, config.noise_gate_dbfs);
}

} // namespace melodick::core
