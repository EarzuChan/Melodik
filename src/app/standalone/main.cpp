#include <iostream>
#include <stdexcept>

#include "melodick/capabilities/backends.h"
#include "melodick/app/session.h"
#include "melodick/io/wav_io.h"

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            std::cerr << "Usage: melodick_standalone_bootstrap <input.wav> <output.wav> [pitch_shift_semitones]\n";
            return 2;
        }

        const std::string input_path = argv[1];
        const std::string output_path = argv[2];
        const double semitones = (argc >= 4) ? std::stod(argv[3]) : 0.0;

        auto config = melodick::capabilities::default_backend_config();
        auto pitch_extractor = melodick::capabilities::create_pitch_extractor(config);
        auto vocoder = melodick::capabilities::create_vocoder(config);

        melodick::app::Session session {pitch_extractor, vocoder};
        const auto wav = melodick::io::read_wav_mono(input_path);
        session.import_audio(wav.mono_samples, wav.sample_rate);
        std::cout << "[stage] imported and analyzed. blobs=" << session.blobs().size() << std::endl;
        if (!session.blobs().empty()) std::cout << "[stage] first_blob_unedited=" << (session.blobs().front().is_unedited() ? 1 : 0) << std::endl;

        if (semitones != 0.0) for (const auto& blob : session.blobs()) session.shift_blob_pitch(blob.id, semitones);

        session.render_all_dirty(64);
        std::cout << "[stage] render complete" << std::endl;

        const auto mixed = session.build_rendered_mixdown();
        std::cout << "[stage] mixdown samples=" << mixed.size() << std::endl;
        melodick::io::write_wav_mono_16(output_path, mixed, session.sample_rate());
        std::cout << "[stage] wav written" << std::endl;

        std::cout << "MeloDick real chain done. blobs=" << session.blobs().size() << " duration=" << session.duration_seconds() << "s output=" << output_path << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "MeloDick bootstrap failed: " << ex.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "MeloDick bootstrap failed: unknown error\n";
        return 1;
    }
}
