#include "test_framework.h"

#include <cmath>
#include <limits>

#include "melodick/core/note_blob.h"

MELODICK_TEST(note_blob_shift_pitch_updates_final_curve) {
    melodick::core::NoteBlob note {};
    note.time = {.start_seconds = 0.0, .end_seconds = 1.0};
    note.original_start_seconds = 0.0;
    note.original_duration_seconds = 1.0;
    note.original_pitch_curve = {
        {.seconds = 0.0, .midi = 60.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.5, .midi = 61.0, .voiced = true, .confidence = 1.0f},
    };
    note.handdraw_patch_midi = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN(),
    };

    melodick::core::NoteBlobOps::shift_pitch(note, 2.0);

    const auto final = note.final_pitch_curve();
    MELODICK_EXPECT_EQ(final.size(), static_cast<std::size_t>(2));
    MELODICK_EXPECT_EQ(final[0].midi, 62.0);
    MELODICK_EXPECT_EQ(final[1].midi, 63.0);
}

MELODICK_TEST(note_blob_stretch_updates_duration_and_ratio) {
    melodick::core::NoteBlob note {};
    note.time = {.start_seconds = 1.0, .end_seconds = 2.0};
    note.original_start_seconds = 1.0;
    note.original_duration_seconds = 1.0;
    note.original_pitch_curve = {
        {.seconds = 1.0, .midi = 60.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 1.5, .midi = 60.0, .voiced = true, .confidence = 1.0f},
    };

    melodick::core::NoteBlobOps::stretch_time(note, 2.0);

    MELODICK_EXPECT_EQ(note.time.end_seconds, 3.0);
    MELODICK_EXPECT_TRUE(note.time_ratio > 1.99 && note.time_ratio < 2.01);
}

MELODICK_TEST(note_blob_pipeline_applies_handdraw_and_line_patch) {
    melodick::core::NoteBlob note {};
    note.time = {.start_seconds = 2.0, .end_seconds = 3.0};
    note.original_start_seconds = 2.0;
    note.original_duration_seconds = 1.0;
    note.original_pitch_curve = {
        {.seconds = 2.0, .midi = 60.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 2.5, .midi = 60.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 3.0, .midi = 60.0, .voiced = true, .confidence = 1.0f},
    };
    note.handdraw_patch_midi = {
        std::numeric_limits<float>::quiet_NaN(),
        1.0f,
        std::numeric_limits<float>::quiet_NaN(),
    };
    note.global_transpose_semitones = 2.0;
    note.line_patches = {
        melodick::core::LinePatch {
            .type = melodick::core::LinePatchType::Glide,
            .start_u = 0.0,
            .end_u = 1.0,
            .start_delta_midi = 0.0,
            .end_delta_midi = 1.0,
        },
    };

    const auto final = note.final_pitch_curve();
    MELODICK_EXPECT_EQ(final.size(), static_cast<std::size_t>(3));
    MELODICK_EXPECT_TRUE(std::fabs(final[0].midi - 62.0) < 1.0e-6);
    MELODICK_EXPECT_TRUE(std::fabs(final[1].midi - 63.5) < 1.0e-6);
    MELODICK_EXPECT_TRUE(std::fabs(final[2].midi - 63.0) < 1.0e-6);
}
