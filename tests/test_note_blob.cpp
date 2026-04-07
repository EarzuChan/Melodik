#include "test_framework.h"

#include "melodick/core/note_blob.h"

MELODICK_TEST(note_blob_shift_pitch_updates_display_and_curve) {
    melodick::core::NoteBlob note {};
    note.time = {.start_seconds = 0.0, .end_seconds = 1.0};
    note.display_pitch_midi = 60.0;
    note.edited_pitch_slice = {
        {.seconds = 0.0, .midi = 60.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.5, .midi = 61.0, .voiced = true, .confidence = 1.0f},
    };

    melodick::core::NoteBlobOps::shift_pitch(note, 2.0);

    MELODICK_EXPECT_EQ(note.display_pitch_midi, 62.0);
    MELODICK_EXPECT_EQ(note.edited_pitch_slice[0].midi, 62.0);
    MELODICK_EXPECT_EQ(note.edited_pitch_slice[1].midi, 63.0);
}

MELODICK_TEST(note_blob_stretch_updates_duration) {
    melodick::core::NoteBlob note {};
    note.time = {.start_seconds = 1.0, .end_seconds = 2.0};
    note.edited_pitch_slice = {
        {.seconds = 1.0, .midi = 60.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 1.5, .midi = 60.0, .voiced = true, .confidence = 1.0f},
    };

    melodick::core::NoteBlobOps::stretch_time(note, 2.0);

    MELODICK_EXPECT_EQ(note.time.end_seconds, 3.0);
    MELODICK_EXPECT_TRUE(note.time_stretch_ratio > 1.99 && note.time_stretch_ratio < 2.01);
}
