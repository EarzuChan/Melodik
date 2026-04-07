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
