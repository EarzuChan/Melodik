#include <iostream>
#include <stdexcept>

#include "melodick/capabilities/backends.h"
#include "melodick/app/session.h"
#include "melodick/io/wav_io.h"
#include "melodick/project/project_state.h"

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            std::cerr << "Usage: melodick_standalone_bootstrap <input.wav> <output.wav> [pitch_delta_midi] [project_out.mldk]\n";
            return 2;
        }

        const std::string input_path = argv[1];
        const std::string output_path = argv[2];
        const double pitch_delta_midi = (argc >= 4) ? std::stod(argv[3]) : 0.0;
        const std::string project_out = (argc >= 5) ? argv[4] : "";

        auto config = melodick::capabilities::default_backend_config();
        auto pitch_extractor = melodick::capabilities::create_pitch_extractor(config);
        auto vocoder = melodick::capabilities::create_vocoder(config);

        melodick::app::Session session {pitch_extractor, vocoder};
        const auto wav = melodick::io::read_wav(input_path);
        const auto track_id = session.import_audio_as_new_track(wav.interleaved_samples, wav.sample_rate, wav.channels, "Lead Vocal");
        const auto& blobs = session.track_blobs(track_id);
        std::cout << "[stage] imported and analyzed. track=" << track_id << " blobs=" << blobs.size() << std::endl;
        if (!blobs.empty()) std::cout << "[stage] first_blob_unedited=" << (blobs.front().is_unedited() ? 1 : 0) << std::endl;

        if (pitch_delta_midi != 0.0) for (const auto& blob : blobs) session.apply_blob_pitch_delta(track_id, blob.id, pitch_delta_midi);

        session.render_all_dirty(64);
        std::cout << "[stage] render complete" << std::endl;

        const auto mixed = session.build_mixdown();
        std::cout << "[stage] mixdown samples=" << mixed.size() << std::endl;
        melodick::io::write_wav_mono_16(output_path, mixed, session.session_sample_rate());
        std::cout << "[stage] wav written" << std::endl;
        if (!project_out.empty()) {
            const auto state = session.capture_project_state();
            melodick::project::save_project_state(project_out, state);
            std::cout << "[stage] project written: " << project_out << std::endl;
        }

        std::cout << "MeloDick real chain done. tracks=" << session.tracks().size() << " duration=" << session.duration_seconds() << "s output=" << output_path << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "MeloDick bootstrap failed: " << ex.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "MeloDick bootstrap failed: unknown error\n";
        return 1;
    }
}
