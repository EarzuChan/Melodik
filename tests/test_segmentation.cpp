#include "test_framework.h"

#include "melodick/capabilities/segmenter.h"

MELODICK_TEST(segmenter_creates_notes_from_pitch_jumps) {
    melodick::capabilities::NoteBlobSegmenter segmenter {};

    melodick::core::PitchSlice f0 {
        {.seconds = 0.00, .midi = 60.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.10, .midi = 60.2, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.20, .midi = 66.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.30, .midi = 66.1, .voiced = true, .confidence = 1.0f},
    };

    const auto notes = segmenter.build_segments(f0);
    MELODICK_EXPECT_EQ(notes.size(), static_cast<std::size_t>(2));
    MELODICK_EXPECT_TRUE(notes[0].link_next.has_value());
    MELODICK_EXPECT_TRUE(notes[1].link_prev.has_value());
}

MELODICK_TEST(segmenter_merges_short_unvoiced_lead_into_following_voiced_blob) {
    melodick::capabilities::NoteBlobSegmenter segmenter {};

    melodick::core::PitchSlice f0 {
        {.seconds = 0.00, .midi = 60.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.10, .midi = 60.1, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.12, .midi = 0.0, .voiced = false, .confidence = 0.0f},
        {.seconds = 0.16, .midi = 0.0, .voiced = false, .confidence = 0.0f},
        {.seconds = 0.20, .midi = 67.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.30, .midi = 67.1, .voiced = true, .confidence = 1.0f},
    };

    const auto notes = segmenter.build_segments(f0);
    MELODICK_EXPECT_EQ(notes.size(), static_cast<std::size_t>(2));
    MELODICK_EXPECT_TRUE(std::fabs(notes[0].time.end_seconds - notes[1].time.start_seconds) < 1.0e-6);
    MELODICK_EXPECT_TRUE(std::fabs(notes[1].time.start_seconds - 0.11) < 1.0e-6);
    MELODICK_EXPECT_TRUE(notes[1].has_voiced_content());
}

MELODICK_TEST(segmenter_keeps_long_unvoiced_runs_as_independent_blobs) {
    melodick::capabilities::NoteBlobSegmenter segmenter {};

    melodick::core::PitchSlice f0 {
        {.seconds = 0.00, .midi = 60.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.10, .midi = 60.1, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.20, .midi = 0.0, .voiced = false, .confidence = 0.0f},
        {.seconds = 0.30, .midi = 0.0, .voiced = false, .confidence = 0.0f},
        {.seconds = 0.40, .midi = 0.0, .voiced = false, .confidence = 0.0f},
        {.seconds = 0.50, .midi = 67.0, .voiced = true, .confidence = 1.0f},
        {.seconds = 0.60, .midi = 67.1, .voiced = true, .confidence = 1.0f},
    };

    const auto notes = segmenter.build_segments(f0);
    MELODICK_EXPECT_EQ(notes.size(), static_cast<std::size_t>(3));
    MELODICK_EXPECT_TRUE(std::fabs(notes[0].time.end_seconds - 0.19) < 1.0e-6);
    MELODICK_EXPECT_TRUE(std::fabs(notes[1].time.start_seconds - 0.19) < 1.0e-6);
    MELODICK_EXPECT_TRUE(std::fabs(notes[1].time.end_seconds - 0.37) < 1.0e-6);
    MELODICK_EXPECT_TRUE(std::fabs(notes[2].time.start_seconds - 0.37) < 1.0e-6);
    MELODICK_EXPECT_TRUE(notes[1].is_unvoiced_only());
    MELODICK_EXPECT_TRUE(!notes[1].link_prev.has_value());
    MELODICK_EXPECT_TRUE(!notes[1].link_next.has_value());
    MELODICK_EXPECT_TRUE(!notes[2].link_prev.has_value());
}
