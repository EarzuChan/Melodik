#pragma once

#include <cstddef>
#include <vector>

namespace melodick::core {

[[nodiscard]] std::vector<float> resample_audio_to_size(const std::vector<float>& input, std::size_t target_size);
[[nodiscard]] std::vector<float> resample_audio_rate(const std::vector<float>& input, int src_rate, int dst_rate);

} // namespace melodick::core
