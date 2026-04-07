#include "test_framework.h"

#include <cstdio>

#include "melodick/project/project_state.h"

MELODICK_TEST(project_state_roundtrip_keeps_blob_and_f0_data) {
    melodick::project::ProjectState state {};
    state.sample_rate = 44100;
    state.duration_seconds = 2.5;
    state.source_audio_path = "samples/sample1.wav";
    state.analysis_f0 = {
        {.seconds = 0.0, .midi = 60.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.01, .midi = 61.0, .voiced = true, .confidence = 0.9f},
    };

    melodick::core::NoteBlob blob {};
    blob.id = 7;
    blob.time = {.start_seconds = 0.2, .end_seconds = 0.9};
    blob.original_start_seconds = 0.2;
    blob.original_end_seconds = 0.8;
    blob.display_pitch_midi = 62.0;
    blob.pitch_offset_semitones = 1.0;
    blob.time_stretch_ratio = 1.2;
    blob.loudness_gain_db = -3.0;
    blob.detached = false;
    blob.edit_revision = 4;
    blob.link_prev = 6;
    blob.link_next = 8;
    blob.original_pitch_slice = {
        {.seconds = 0.2, .midi = 61.0, .voiced = true, .confidence = 1.0f},
    };
    blob.edited_pitch_slice = {
        {.seconds = 0.2, .midi = 62.0, .voiced = true, .confidence = 1.0f},
    };
    state.blobs.push_back(blob);

    const std::string temp_path = "test_project_roundtrip.mdkproj";
    melodick::project::save_project_state(temp_path, state);
    const auto loaded = melodick::project::load_project_state(temp_path);

    MELODICK_EXPECT_EQ(loaded.sample_rate, state.sample_rate);
    MELODICK_EXPECT_EQ(loaded.analysis_f0.size(), state.analysis_f0.size());
    MELODICK_EXPECT_EQ(loaded.blobs.size(), state.blobs.size());
    MELODICK_EXPECT_EQ(loaded.blobs.front().id, state.blobs.front().id);
    MELODICK_EXPECT_EQ(loaded.blobs.front().edit_revision, state.blobs.front().edit_revision);

    std::remove(temp_path.c_str());
}
