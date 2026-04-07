#include "test_framework.h"

#include <cmath>
#include <cstdio>
#include <memory>

#include "melodick/app/session.h"
#include "melodick/project/project_state.h"

namespace {

class FakePitchExtractor final : public melodick::capabilities::IPitchExtractor {
public:
    melodick::core::PitchSlice extract_f0(const std::vector<float>&, int) override {
        return {
            {.seconds = 0.0, .midi = 60.0, .voiced = true, .confidence = 1.0f},
            {.seconds = 0.2, .midi = 60.0, .voiced = true, .confidence = 1.0f},
            {.seconds = 0.4, .midi = 64.0, .voiced = true, .confidence = 1.0f},
            {.seconds = 0.6, .midi = 64.0, .voiced = true, .confidence = 1.0f},
        };
    }
};

class FakeVocoder final : public melodick::capabilities::IVocoder {
public:
    std::size_t render_calls {0};

    std::vector<float> render_note_audio(const melodick::core::NoteBlob& note, const std::vector<float>&, int sample_rate) override {
        ++render_calls;
        const auto count = static_cast<std::size_t>(note.duration() * sample_rate);
        return std::vector(count, 0.2f);
    }
};

} // namespace

MELODICK_TEST(session_dirty_is_derived_and_lazy_render_converges) {
    auto pitch = std::make_shared<FakePitchExtractor>();
    auto vocoder = std::make_shared<FakeVocoder>();
    melodick::app::Session session {pitch, vocoder};

    std::vector audio(44100 * 2, 0.1f);
    session.import_audio(audio, 44100);
    MELODICK_EXPECT_TRUE(!session.notes().empty());
    MELODICK_EXPECT_TRUE(!session.dirty_timeline().dirty_ranges().empty());

    const auto note_id = session.notes().front().id;
    session.shift_blob_pitch(note_id, 1.0);

    auto plan = session.plan_render_from(0.0, 8);
    MELODICK_EXPECT_TRUE(!plan.empty());

    session.render_all_dirty(8);
    MELODICK_EXPECT_TRUE(session.plan_render_from(0.0, 8).empty());

    const auto mixed = session.build_rendered_mixdown();
    MELODICK_EXPECT_TRUE(!mixed.empty());
}

MELODICK_TEST(session_unedited_is_passthrough_and_stretch_uses_resynthesis) {
    auto pitch = std::make_shared<FakePitchExtractor>();
    auto vocoder = std::make_shared<FakeVocoder>();
    melodick::app::Session session {pitch, vocoder};

    std::vector audio(44100, 0.1f);
    session.import_audio(audio, 44100);
    MELODICK_EXPECT_TRUE(!session.blobs().empty());

    session.render_all_dirty(8);
    MELODICK_EXPECT_EQ(vocoder->render_calls, static_cast<std::size_t>(0));

    auto mixed = session.build_rendered_mixdown();
    MELODICK_EXPECT_EQ(mixed.size(), audio.size());
    MELODICK_EXPECT_TRUE(std::fabs(static_cast<double>(mixed[100]) - 0.1) < 1.0e-4);

    const auto blob_id = session.blobs().front().id;
    const auto old_duration = session.blobs().front().duration();
    session.stretch_blob_time(blob_id, old_duration * 1.5);
    session.render_all_dirty(8);
    MELODICK_EXPECT_TRUE(vocoder->render_calls > 0);

    mixed = session.build_rendered_mixdown();
    MELODICK_EXPECT_EQ(mixed.size(), audio.size());
}

MELODICK_TEST(session_project_state_save_load_roundtrip) {
    auto pitch = std::make_shared<FakePitchExtractor>();
    auto vocoder = std::make_shared<FakeVocoder>();
    melodick::app::Session session {pitch, vocoder};

    std::vector audio(44100, 0.1f);
    session.import_audio(audio, 44100);
    MELODICK_EXPECT_TRUE(!session.blobs().empty());
    session.shift_blob_pitch(session.blobs().front().id, 2.0);

    const std::string source_path = "samples/sample1.wav";
    const auto state = session.capture_project_state(source_path);
    const std::string temp_path = "test_session_roundtrip.mdkproj";
    melodick::project::save_project_state(temp_path, state);
    const auto loaded = melodick::project::load_project_state(temp_path);

    auto pitch2 = std::make_shared<FakePitchExtractor>();
    auto vocoder2 = std::make_shared<FakeVocoder>();
    melodick::app::Session restored {pitch2, vocoder2};
    restored.restore_project_state(loaded, audio, loaded.sample_rate);

    MELODICK_EXPECT_EQ(restored.blobs().size(), session.blobs().size());
    MELODICK_EXPECT_EQ(restored.analysis_f0().size(), session.analysis_f0().size());
    MELODICK_EXPECT_TRUE(!restored.dirty_timeline().dirty_ranges().empty());

    std::remove(temp_path.c_str());
}
