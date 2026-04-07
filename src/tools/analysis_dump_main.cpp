#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "melodick/capabilities/backends.h"
#include "melodick/capabilities/segmenter.h"
#include "melodick/io/wav_io.h"

namespace {

float midi_to_hz(double midi) {
    if (midi <= 0.0) {
        return 0.0f;
    }
    return 440.0f * std::pow(2.0f, static_cast<float>((midi - 69.0) / 12.0));
}

std::string json_escape(std::string value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

void write_meta_json(const std::filesystem::path& out_path,
                     const std::string& wav_path,
                     int sample_rate,
                     std::size_t sample_count,
                     std::size_t f0_frames,
                     std::size_t note_count) {
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open " + out_path.string());
    }
    const double duration = sample_rate > 0
        ? (static_cast<double>(sample_count) / static_cast<double>(sample_rate))
        : 0.0;
    out << "{\n";
    out << "  \"source_wav\": \"" << json_escape(wav_path) << "\",\n";
    out << "  \"sample_rate\": " << sample_rate << ",\n";
    out << "  \"sample_count\": " << sample_count << ",\n";
    out << "  \"duration_seconds\": " << std::fixed << std::setprecision(6) << duration << ",\n";
    out << "  \"f0_frames\": " << f0_frames << ",\n";
    out << "  \"note_count\": " << note_count << "\n";
    out << "}\n";
}

void write_f0_csv(const std::filesystem::path& out_path, const melodick::core::PitchSlice& f0) {
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open " + out_path.string());
    }
    out << "frame,time_s,midi,hz,voiced,confidence\n";
    out << std::fixed << std::setprecision(6);
    for (std::size_t i = 0; i < f0.size(); ++i) {
        const auto& p = f0[i];
        out << i << ','
            << p.seconds << ','
            << p.midi << ','
            << midi_to_hz(p.midi) << ','
            << (p.voiced ? 1 : 0) << ','
            << p.confidence << '\n';
    }
}

void write_segments_csv(const std::filesystem::path& out_path, const std::vector<melodick::core::NoteBlob>& notes) {
    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open " + out_path.string());
    }
    out << "id,start_s,end_s,duration_s,display_midi,display_hz,detached,link_prev,link_next\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& n : notes) {
        out << n.id << ','
            << n.time.start_seconds << ','
            << n.time.end_seconds << ','
            << n.duration() << ','
            << n.display_pitch_midi << ','
            << midi_to_hz(n.display_pitch_midi) << ','
            << (n.detached ? 1 : 0) << ',';
        if (n.link_prev.has_value()) {
            out << n.link_prev.value();
        }
        out << ',';
        if (n.link_next.has_value()) {
            out << n.link_next.value();
        }
        out << '\n';
    }
}

void write_waveform_csv(const std::filesystem::path& out_path,
                        const std::vector<float>& mono,
                        int sample_rate,
                        int points_per_second) {
    if (sample_rate <= 0) {
        throw std::invalid_argument("sample_rate must be positive");
    }
    const int pps = std::max(16, points_per_second);
    const std::size_t window = std::max<std::size_t>(1, static_cast<std::size_t>(sample_rate / pps));

    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open " + out_path.string());
    }
    out << "index,time_s,min_amp,max_amp,rms\n";
    out << std::fixed << std::setprecision(6);

    std::size_t idx = 0;
    for (std::size_t start = 0; start < mono.size(); start += window) {
        const std::size_t end = std::min<std::size_t>(start + window, mono.size());
        if (end <= start) {
            continue;
        }

        float min_v = 1.0f;
        float max_v = -1.0f;
        double sq = 0.0;
        for (std::size_t i = start; i < end; ++i) {
            const float v = std::clamp(mono[i], -1.0f, 1.0f);
            min_v = std::min(min_v, v);
            max_v = std::max(max_v, v);
            sq += static_cast<double>(v) * static_cast<double>(v);
        }
        const double rms = std::sqrt(sq / static_cast<double>(end - start));
        const double mid_seconds =
            (static_cast<double>(start + end) * 0.5) / static_cast<double>(sample_rate);
        out << idx++ << ','
            << mid_seconds << ','
            << min_v << ','
            << max_v << ','
            << rms << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            std::cerr << "Usage: melodick_analysis_dump <input.wav> <out_dir> [wave_points_per_second]\n";
            return 2;
        }

        const std::string input_path = argv[1];
        const std::filesystem::path out_dir = argv[2];
        const int wave_points_per_second = (argc >= 4) ? std::stoi(argv[3]) : 240;

        std::filesystem::create_directories(out_dir);

        const auto wav = melodick::io::read_wav_mono(input_path);
        auto cfg = melodick::capabilities::default_backend_config();
        auto pitch = melodick::capabilities::create_pitch_extractor(cfg);
        melodick::capabilities::NoteBlobSegmenter segmenter {};

        const auto f0 = pitch->extract_f0(wav.mono_samples, wav.sample_rate);
        const auto notes = segmenter.build_segments(f0);

        const auto meta_path = out_dir / "analysis_meta.json";
        const auto f0_path = out_dir / "analysis_f0.csv";
        const auto segments_path = out_dir / "analysis_segments.csv";
        const auto waveform_path = out_dir / "analysis_waveform.csv";

        write_meta_json(meta_path, input_path, wav.sample_rate, wav.mono_samples.size(), f0.size(), notes.size());
        write_f0_csv(f0_path, f0);
        write_segments_csv(segments_path, notes);
        write_waveform_csv(waveform_path, wav.mono_samples, wav.sample_rate, wave_points_per_second);

        std::cout << "analysis dump done: "
                  << "sr=" << wav.sample_rate
                  << " samples=" << wav.mono_samples.size()
                  << " f0=" << f0.size()
                  << " notes=" << notes.size()
                  << " out_dir=" << out_dir.string()
                  << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "analysis dump failed: " << ex.what() << '\n';
        return 1;
    }
}
