#pragma once

#include <vector>

namespace melodick::core {

struct PitchPoint {
    double seconds {0.0};
    double midi {0.0};
    bool voiced {false};
    float confidence {0.0f};
};

using PitchSlice = std::vector<PitchPoint>;

} // namespace melodick::core

