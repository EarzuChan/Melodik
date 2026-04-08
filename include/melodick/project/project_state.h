#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "melodick/core/note_blob.h"

namespace melodick::project {

struct TrackProjectState {
    std::int64_t id {0};
    std::string name {};
    bool mute {false};
    bool solo {false};
    double gain_db {0.0};
    double duration_seconds {0.0};
    std::vector<core::NoteBlob> blobs {};
};

struct ProjectState {
    int session_sample_rate {44100};
    double duration_seconds {0.0};
    std::vector<TrackProjectState> tracks {};
};

void save_project_state(const std::string& path, const ProjectState& state);
ProjectState load_project_state(const std::string& path);

} // namespace melodick::project
