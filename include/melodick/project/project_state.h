#pragma once

#include <string>
#include <vector>

#include "melodick/core/note_blob.h"
#include "melodick/core/pitch_data.h"

namespace melodick::project {

struct ProjectState {
    int sample_rate {0};
    double duration_seconds {0.0};
    std::string source_audio_path {};
    core::PitchSlice analysis_f0 {};
    std::vector<core::NoteBlob> blobs {};
};

void save_project_state(const std::string& path, const ProjectState& state);
ProjectState load_project_state(const std::string& path);

} // namespace melodick::project
