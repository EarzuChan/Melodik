#include "test_framework.h"

#include <cstdio>
#include <limits>

#include "melodick/project/project_state.h"

MELODICK_TEST(project_state_roundtrip_keeps_blob_pipeline_data) {
    melodick::project::ProjectState state {};
    state.session_sample_rate = 44100;
    state.duration_seconds = 2.5;

    melodick::project::TrackProjectState track {};
    track.id = 101;
    track.name = "Lead";
    track.mute = false;
    track.solo = true;
    track.gain_db = -1.5;
    track.duration_seconds = 2.5;

    melodick::core::NoteBlob blob {};
    blob.id = 7;
    blob.time = {.start_seconds = 0.2, .end_seconds = 0.9};
    blob.original_start_seconds = 0.2;
    blob.original_duration_seconds = 0.6;
    blob.global_pitch_delta_midi = 1.0;
    blob.time_ratio = 1.2;
    blob.loudness_gain_db = -3.0;
    blob.detached = false;
    blob.edit_revision = 4;
    blob.link_prev = 6;
    blob.link_next = 8;
    blob.original_pitch_curve = {
        {.seconds = 0.2, .midi = 61.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.5, .midi = 61.5, .voiced = true, .confidence = 1.0f},
    };
    blob.source_f0_hz = {277.18f, 285.31f};
    blob.source_voiced_probability = {0.9f, 0.8f};
    blob.handdraw_patch_midi = {
        std::numeric_limits<float>::quiet_NaN(),
        0.5f,
    };
    blob.line_patches = {
        melodick::core::LinePatch {
            .type = melodick::core::LinePatchType::Glide,
            .start_u = 0.0,
            .end_u = 1.0,
            .start_delta_midi = 0.0,
            .end_delta_midi = 0.5,
        },
    };
    blob.source_audio_44k = {0.1f, 0.2f, -0.1f};
    blob.cached_source_mel_bins = 128;
    blob.cached_source_mel_frames = 2;
    blob.cached_source_mel_log = {1.0f, 2.0f, 3.0f};
    track.blobs.push_back(blob);
    state.tracks.push_back(track);

    const std::string temp_path = "test_project_roundtrip.mldk";
    melodick::project::save_project_state(temp_path, state);
    const auto loaded = melodick::project::load_project_state(temp_path);

    MELODICK_EXPECT_EQ(loaded.session_sample_rate, state.session_sample_rate);
    MELODICK_EXPECT_EQ(loaded.tracks.size(), state.tracks.size());
    MELODICK_EXPECT_EQ(loaded.tracks.front().id, state.tracks.front().id);
    MELODICK_EXPECT_EQ(loaded.tracks.front().blobs.size(), state.tracks.front().blobs.size());
    MELODICK_EXPECT_EQ(loaded.tracks.front().blobs.front().id, state.tracks.front().blobs.front().id);
    MELODICK_EXPECT_EQ(loaded.tracks.front().blobs.front().edit_revision, state.tracks.front().blobs.front().edit_revision);
    MELODICK_EXPECT_EQ(loaded.tracks.front().blobs.front().original_pitch_curve.size(), state.tracks.front().blobs.front().original_pitch_curve.size());
    MELODICK_EXPECT_EQ(loaded.tracks.front().blobs.front().source_f0_hz.size(), state.tracks.front().blobs.front().source_f0_hz.size());
    MELODICK_EXPECT_EQ(loaded.tracks.front().blobs.front().source_voiced_probability.size(), state.tracks.front().blobs.front().source_voiced_probability.size());
    MELODICK_EXPECT_EQ(loaded.tracks.front().blobs.front().handdraw_patch_midi.size(), state.tracks.front().blobs.front().handdraw_patch_midi.size());
    MELODICK_EXPECT_EQ(loaded.tracks.front().blobs.front().line_patches.size(), state.tracks.front().blobs.front().line_patches.size());
    MELODICK_EXPECT_EQ(loaded.tracks.front().blobs.front().source_audio_44k.size(), state.tracks.front().blobs.front().source_audio_44k.size());
    MELODICK_EXPECT_TRUE(loaded.tracks.front().blobs.front().cached_source_mel_log.empty());
    MELODICK_EXPECT_EQ(loaded.tracks.front().blobs.front().cached_source_mel_bins, 0);
    MELODICK_EXPECT_EQ(loaded.tracks.front().blobs.front().cached_source_mel_frames, 0);

    std::remove(temp_path.c_str());
}
