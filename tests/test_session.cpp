#include "test_framework.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

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
    std::size_t prepare_calls {0};

    void prepare_blob(melodick::core::NoteBlob& note, int) override {
        ++prepare_calls;
        note.source_mel_bins = 128;
        note.source_mel_frames = static_cast<int>(std::max<std::size_t>(1, note.source_audio_44k.size() / 512));
        note.source_mel_log.assign(static_cast<std::size_t>(note.source_mel_bins) * static_cast<std::size_t>(note.source_mel_frames), 0.0f);
    }

    std::vector<float> render_group_audio(const std::vector<melodick::core::NoteBlob>& notes, int sample_rate) override {
        ++render_calls;
        double d = 0.0;
        if (!notes.empty()) {
            double start = notes.front().time.start_seconds;
            double end = notes.front().time.end_seconds;
            for (const auto& note : notes) {
                start = std::min(start, note.time.start_seconds);
                end = std::max(end, note.time.end_seconds);
            }
            d = std::max(0.0, end - start);
        }
        const auto count = static_cast<std::size_t>(d * static_cast<double>(sample_rate));
        return std::vector<float>(count, 0.2f);
    }
};

std::vector<float> make_stereo_constant(const std::size_t frames, const float left, const float right) {
    std::vector<float> out(frames * 2, 0.0f);
    for (std::size_t i = 0; i < frames; ++i) {
        out[i * 2] = left;
        out[i * 2 + 1] = right;
    }
    return out;
}

} // namespace

MELODICK_TEST(session_multitrack_dirty_is_derived_and_lazy_render_converges) {
    auto pitch = std::make_shared<FakePitchExtractor>();
    auto vocoder = std::make_shared<FakeVocoder>();
    melodick::app::Session session {pitch, vocoder};

    std::vector<float> mono_44k(44100 * 2, 0.1f);
    const auto track_1 = session.import_audio_as_new_track(mono_44k, 44100, 1, "T1");

    const auto stereo_48k = make_stereo_constant(48000, 0.2f, 0.0f);
    const auto track_2 = session.import_audio_as_new_track(stereo_48k, 48000, 2, "T2");

    MELODICK_EXPECT_EQ(session.tracks().size(), static_cast<std::size_t>(2));
    MELODICK_EXPECT_TRUE(!session.track_dirty_timeline(track_1).dirty_ranges().empty());
    MELODICK_EXPECT_TRUE(!session.track_dirty_timeline(track_2).dirty_ranges().empty());

    const auto blob_id = session.track_blobs(track_1).front().id;
    session.shift_blob_pitch(track_1, blob_id, 1.0);

    const auto plans = session.plan_render_from(0.0, 8);
    MELODICK_EXPECT_TRUE(!plans.empty());

    session.render_all_dirty(8);
    MELODICK_EXPECT_TRUE(session.track_dirty_timeline(track_1).dirty_ranges().empty());
    MELODICK_EXPECT_TRUE(session.track_dirty_timeline(track_2).dirty_ranges().empty());

    const auto mix = session.build_mixdown();
    MELODICK_EXPECT_TRUE(!mix.empty());
    MELODICK_EXPECT_EQ(session.session_sample_rate(), 44100);

    const auto track2_audio = session.build_track_audio(track_2);
    MELODICK_EXPECT_TRUE(track2_audio.size() > 1000);
    MELODICK_EXPECT_TRUE(std::fabs(static_cast<double>(track2_audio[100]) - 0.1) < 2.0e-3);
}

MELODICK_TEST(session_unedited_passthrough_and_stretch_uses_resynthesis) {
    auto pitch = std::make_shared<FakePitchExtractor>();
    auto vocoder = std::make_shared<FakeVocoder>();
    melodick::app::Session session {pitch, vocoder};

    std::vector<float> mono_44k(44100, 0.1f);
    const auto track_id = session.import_audio_as_new_track(mono_44k, 44100, 1, "Lead");

    session.render_all_dirty(8);
    MELODICK_EXPECT_EQ(vocoder->render_calls, static_cast<std::size_t>(0));

    auto audio = session.build_track_audio(track_id);
    MELODICK_EXPECT_TRUE(audio.size() > 1000);
    MELODICK_EXPECT_TRUE(std::fabs(static_cast<double>(audio[100]) - 0.1) < 1.0e-4);

    const auto blob_id = session.track_blobs(track_id).front().id;
    const auto old_duration = session.track_blobs(track_id).front().duration();
    session.stretch_blob_time(track_id, blob_id, old_duration * 1.5);
    session.render_all_dirty(8);
    MELODICK_EXPECT_TRUE(vocoder->render_calls > 0);
}

MELODICK_TEST(session_track_supports_inserting_audio_at_arbitrary_time) {
    auto pitch = std::make_shared<FakePitchExtractor>();
    auto vocoder = std::make_shared<FakeVocoder>();
    melodick::app::Session session {pitch, vocoder};

    std::vector<float> base_audio(44100, 0.1f);
    const auto track_id = session.import_audio_as_new_track(base_audio, 44100, 1, "Lead");
    const auto before_count = session.track_blobs(track_id).size();

    const auto insert_audio = make_stereo_constant(48000, 0.2f, 0.0f);
    session.insert_audio_to_track(track_id, insert_audio, 48000, 2, 1.0);

    const auto& blobs = session.track_blobs(track_id);
    MELODICK_EXPECT_TRUE(blobs.size() > before_count);
    bool has_inserted_blob = false;
    for (const auto& b : blobs) {
        if (b.time.start_seconds >= 1.0) {
            has_inserted_blob = true;
            break;
        }
    }
    MELODICK_EXPECT_TRUE(has_inserted_blob);

    session.render_all_dirty(8);
    const auto audio = session.build_track_audio(track_id);
    MELODICK_EXPECT_TRUE(audio.size() > 1000);
}

MELODICK_TEST(session_project_state_save_load_roundtrip) {
    auto pitch = std::make_shared<FakePitchExtractor>();
    auto vocoder = std::make_shared<FakeVocoder>();
    melodick::app::Session session {pitch, vocoder};

    std::vector<float> mono_44k(44100, 0.1f);
    const auto t1 = session.import_audio_as_new_track(mono_44k, 44100, 1, "Lead");

    const auto stereo_48k = make_stereo_constant(48000, 0.05f, 0.15f);
    const auto t2 = session.import_audio_as_new_track(stereo_48k, 48000, 2, "Back");

    session.shift_blob_pitch(t1, session.track_blobs(t1).front().id, 2.0);
    session.set_track_solo(t1, true);

    const auto state = session.capture_project_state();
    const std::string temp_path = "test_session_roundtrip.melodick.db";
    melodick::project::save_project_state(temp_path, state);
    const auto loaded = melodick::project::load_project_state(temp_path);

    auto pitch2 = std::make_shared<FakePitchExtractor>();
    auto vocoder2 = std::make_shared<FakeVocoder>();
    melodick::app::Session restored {pitch2, vocoder2};
    restored.restore_project_state(loaded);

    MELODICK_EXPECT_EQ(restored.tracks().size(), session.tracks().size());
    MELODICK_EXPECT_EQ(restored.track_blobs(t1).size(), session.track_blobs(t1).size());
    MELODICK_EXPECT_TRUE(!restored.track_dirty_timeline(t1).dirty_ranges().empty());
    (void)t2;

    std::remove(temp_path.c_str());
}

MELODICK_TEST(session_mixdown_respects_mute_solo_and_track_gain) {
    auto pitch = std::make_shared<FakePitchExtractor>();
    auto vocoder = std::make_shared<FakeVocoder>();
    melodick::app::Session session {pitch, vocoder};

    std::vector<float> t1_audio(44100, 0.1f);
    std::vector<float> t2_audio(44100, 0.2f);
    const auto t1 = session.import_audio_as_new_track(t1_audio, 44100, 1, "T1");
    const auto t2 = session.import_audio_as_new_track(t2_audio, 44100, 1, "T2");
    session.render_all_dirty(8);

    auto mix = session.build_mixdown();
    MELODICK_EXPECT_TRUE(std::fabs(static_cast<double>(mix[100]) - 0.3) < 1.0e-3);

    session.set_track_mute(t2, true);
    mix = session.build_mixdown();
    MELODICK_EXPECT_TRUE(std::fabs(static_cast<double>(mix[100]) - 0.1) < 1.0e-3);

    session.set_track_mute(t2, false);
    session.set_track_solo(t2, true);
    mix = session.build_mixdown();
    MELODICK_EXPECT_TRUE(std::fabs(static_cast<double>(mix[100]) - 0.2) < 1.0e-3);

    session.set_track_gain_db(t2, -6.0206);
    mix = session.build_mixdown();
    MELODICK_EXPECT_TRUE(std::fabs(static_cast<double>(mix[100]) - 0.1) < 2.0e-3);

    session.set_track_solo(t2, false);
    (void)t1;
}
