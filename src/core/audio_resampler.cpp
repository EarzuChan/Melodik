#include "melodick/core/audio_resampler.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <memory>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <unordered_map>

namespace melodick::core {

namespace {

constexpr double kKernelRadius = 10.0;
constexpr double kEps = 1.0e-12;
constexpr int kPhaseCount = 2048;

int reflect_index(int i, const int n) {
    if (n <= 1) {
        return 0;
    }
    const int period = 2 * n - 2;
    if (period <= 0) {
        return 0;
    }

    i %= period;
    if (i < 0) {
        i += period;
    }
    if (i >= n) {
        i = period - i;
    }
    return i;
}

double sinc(const double x) {
    if (std::fabs(x) <= kEps) {
        return 1.0;
    }
    const double pix = std::numbers::pi_v<double> * x;
    return std::sin(pix) / pix;
}

double blackman_window(const double x, const double radius) {
    if (radius <= 0.0) {
        return 0.0;
    }
    const double normalized = std::clamp(x / radius, 0.0, 1.0);
    return 0.42
        + 0.5 * std::cos(std::numbers::pi_v<double> * normalized)
        + 0.08 * std::cos(2.0 * std::numbers::pi_v<double> * normalized);
}

struct KernelTable {
    int half_taps {0};
    std::vector<float> weights {};
};

KernelTable build_kernel_table(const double cutoff) {
    const double clamped_cutoff = std::clamp(cutoff, kEps, 1.0);
    const double support = kKernelRadius / clamped_cutoff;
    const int half_taps = std::max(1, static_cast<int>(std::ceil(support)));
    const int taps_per_phase = half_taps * 2 + 1;

    KernelTable table {};
    table.half_taps = half_taps;
    table.weights.resize(static_cast<std::size_t>(kPhaseCount) * static_cast<std::size_t>(taps_per_phase), 0.0f);

    for (int phase = 0; phase < kPhaseCount; ++phase) {
        const double frac = static_cast<double>(phase) / static_cast<double>(kPhaseCount);
        double sum = 0.0;

        for (int tap = -half_taps; tap <= half_taps; ++tap) {
            const double distance = frac - static_cast<double>(tap);
            const double abs_distance = std::fabs(distance);
            double weight = 0.0;
            if (abs_distance <= support) {
                weight = clamped_cutoff * sinc(clamped_cutoff * distance) * blackman_window(abs_distance, support);
            }

            const auto index = static_cast<std::size_t>(phase) * static_cast<std::size_t>(taps_per_phase)
                + static_cast<std::size_t>(tap + half_taps);
            table.weights[index] = static_cast<float>(weight);
            sum += weight;
        }

        if (std::fabs(sum) <= kEps) {
            continue;
        }
        const auto inv_sum = static_cast<float>(1.0 / sum);
        const auto row_start = static_cast<std::size_t>(phase) * static_cast<std::size_t>(taps_per_phase);
        for (int tap_index = 0; tap_index < taps_per_phase; ++tap_index) {
            table.weights[row_start + static_cast<std::size_t>(tap_index)] *= inv_sum;
        }
    }

    return table;
}

const KernelTable& get_kernel_table(const double cutoff) {
    static std::mutex cache_mutex {};
    static std::unordered_map<std::uint64_t, std::shared_ptr<KernelTable>> cache {};

    const auto clamped_cutoff = std::clamp(cutoff, kEps, 1.0);
    const auto key = static_cast<std::uint64_t>(std::llround(clamped_cutoff * 1'000'000.0));

    {
        std::scoped_lock lock {cache_mutex};
        if (const auto it = cache.find(key); it != cache.end()) {
            return *it->second;
        }
    }

    auto table = std::make_shared<KernelTable>(build_kernel_table(clamped_cutoff));
    std::scoped_lock lock {cache_mutex};
    auto [it, inserted] = cache.emplace(key, table);
    if (!inserted) {
        return *it->second;
    }
    return *table;
}

} // namespace

std::vector<float> resample_audio_to_size(const std::vector<float>& input, const std::size_t target_size) {
    if (target_size == 0) {
        return {};
    }
    if (input.empty()) {
        return std::vector<float>(target_size, 0.0f);
    }
    if (input.size() == target_size) {
        return input;
    }
    if (input.size() == 1) {
        return std::vector<float>(target_size, input.front());
    }
    if (target_size == 1) {
        return {input.front()};
    }

    const double ratio = static_cast<double>(target_size) / static_cast<double>(input.size());
    const double cutoff = std::min(1.0, ratio);
    const auto& kernel = get_kernel_table(cutoff);
    const int half_taps = kernel.half_taps;
    const int taps_per_phase = half_taps * 2 + 1;
    std::vector<float> output(target_size, 0.0f);
    double source_pos = 0.5 / ratio - 0.5;
    const double source_step = 1.0 / ratio;

    for (std::size_t i = 0; i < target_size; ++i) {
        const int base = static_cast<int>(std::floor(source_pos));
        const double frac = source_pos - static_cast<double>(base);
        const auto phase = static_cast<std::size_t>(std::clamp(
            static_cast<int>(std::llround(frac * static_cast<double>(kPhaseCount))),
            0,
            kPhaseCount - 1));
        const auto row = kernel.weights.data() + phase * static_cast<std::size_t>(taps_per_phase);
        double accum = 0.0;
        for (int tap = -half_taps; tap <= half_taps; ++tap) {
            const int reflected = reflect_index(base + tap, static_cast<int>(input.size()));
            const float weight = row[static_cast<std::size_t>(tap + half_taps)];
            accum += static_cast<double>(input[static_cast<std::size_t>(reflected)]) * static_cast<double>(weight);
        }

        output[i] = static_cast<float>(accum);
        source_pos += source_step;
    }

    return output;
}

std::vector<float> resample_audio_rate(const std::vector<float>& input, const int src_rate, const int dst_rate) {
    if (src_rate <= 0 || dst_rate <= 0) {
        throw std::invalid_argument("invalid sample rate");
    }
    if (input.empty() || src_rate == dst_rate) {
        return input;
    }

    const double ratio = static_cast<double>(dst_rate) / static_cast<double>(src_rate);
    const auto target_size = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::llround(static_cast<double>(input.size()) * ratio)));
    return resample_audio_to_size(input, target_size);
}

} // namespace melodick::core
