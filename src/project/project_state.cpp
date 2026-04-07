#include "melodick/project/project_state.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace melodick::project {

namespace {

constexpr const char* kHeader = "MELODICK_PROJECT_V1";

std::string read_required_line(std::istream& in) {
    std::string line;
    if (!std::getline(in, line)) {
        throw std::runtime_error("unexpected end of project file");
    }
    return line;
}

std::string prefix_value(const std::string& line, const std::string& key) {
    const auto prefix = key + "=";
    if (line.rfind(prefix, 0) != 0) {
        throw std::runtime_error("project parse error: expected " + key);
    }
    return line.substr(prefix.size());
}

bool parse_bool_token(const std::string& token) {
    if (token == "1" || token == "true") {
        return true;
    }
    if (token == "0" || token == "false") {
        return false;
    }
    throw std::runtime_error("project parse error: invalid bool token");
}

core::PitchPoint parse_pitch_line(const std::string& line, const std::string& prefix) {
    if (line.rfind(prefix, 0) != 0) {
        throw std::runtime_error("project parse error: pitch line prefix mismatch");
    }

    std::istringstream iss(line.substr(prefix.size()));
    core::PitchPoint p {};
    std::string voiced {};
    if (!(iss >> p.seconds >> p.midi >> voiced >> p.confidence)) {
        throw std::runtime_error("project parse error: invalid pitch point");
    }
    p.voiced = parse_bool_token(voiced);
    return p;
}

void write_pitch_line(std::ostream& out, const char* prefix, const core::PitchPoint& p) {
    out << prefix << ' '
        << p.seconds << ' '
        << p.midi << ' '
        << (p.voiced ? 1 : 0) << ' '
        << p.confidence << '\n';
}

} // namespace

void save_project_state(const std::string& path, const ProjectState& state) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open project file for write: " + path);
    }

    out.setf(std::ios::fixed);
    out.precision(9);

    out << kHeader << '\n';
    out << "sample_rate=" << state.sample_rate << '\n';
    out << "duration_seconds=" << state.duration_seconds << '\n';
    out << "source_audio_path=" << state.source_audio_path << '\n';

    out << "analysis_f0_count=" << state.analysis_f0.size() << '\n';
    for (const auto& p : state.analysis_f0) {
        write_pitch_line(out, "f0", p);
    }

    out << "blob_count=" << state.blobs.size() << '\n';
    for (const auto& b : state.blobs) {
        out << "blob_begin\n";
        out << "id=" << b.id << '\n';
        out << "time_start=" << b.time.start_seconds << '\n';
        out << "time_end=" << b.time.end_seconds << '\n';
        out << "original_start=" << b.original_start_seconds << '\n';
        out << "original_end=" << b.original_end_seconds << '\n';
        out << "display_pitch_midi=" << b.display_pitch_midi << '\n';
        out << "pitch_offset_semitones=" << b.pitch_offset_semitones << '\n';
        out << "time_stretch_ratio=" << b.time_stretch_ratio << '\n';
        out << "loudness_gain_db=" << b.loudness_gain_db << '\n';
        out << "detached=" << (b.detached ? 1 : 0) << '\n';
        out << "edit_revision=" << b.edit_revision << '\n';
        out << "link_prev=" << (b.link_prev.has_value() ? b.link_prev.value() : -1) << '\n';
        out << "link_next=" << (b.link_next.has_value() ? b.link_next.value() : -1) << '\n';

        out << "original_pitch_count=" << b.original_pitch_slice.size() << '\n';
        for (const auto& p : b.original_pitch_slice) {
            write_pitch_line(out, "op", p);
        }
        out << "edited_pitch_count=" << b.edited_pitch_slice.size() << '\n';
        for (const auto& p : b.edited_pitch_slice) {
            write_pitch_line(out, "ep", p);
        }
        out << "blob_end\n";
    }
}

ProjectState load_project_state(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open project file for read: " + path);
    }

    if (read_required_line(in) != kHeader) {
        throw std::runtime_error("unsupported project format header");
    }

    ProjectState out {};
    out.sample_rate = std::stoi(prefix_value(read_required_line(in), "sample_rate"));
    out.duration_seconds = std::stod(prefix_value(read_required_line(in), "duration_seconds"));
    out.source_audio_path = prefix_value(read_required_line(in), "source_audio_path");

    const auto f0_count = static_cast<std::size_t>(std::stoull(prefix_value(read_required_line(in), "analysis_f0_count")));
    out.analysis_f0.reserve(f0_count);
    for (std::size_t i = 0; i < f0_count; ++i) {
        out.analysis_f0.push_back(parse_pitch_line(read_required_line(in), "f0 "));
    }

    const auto blob_count = static_cast<std::size_t>(std::stoull(prefix_value(read_required_line(in), "blob_count")));
    out.blobs.reserve(blob_count);
    for (std::size_t i = 0; i < blob_count; ++i) {
        if (read_required_line(in) != "blob_begin") {
            throw std::runtime_error("project parse error: missing blob_begin");
        }

        core::NoteBlob b {};
        b.id = std::stoll(prefix_value(read_required_line(in), "id"));
        b.time.start_seconds = std::stod(prefix_value(read_required_line(in), "time_start"));
        b.time.end_seconds = std::stod(prefix_value(read_required_line(in), "time_end"));
        b.original_start_seconds = std::stod(prefix_value(read_required_line(in), "original_start"));
        b.original_end_seconds = std::stod(prefix_value(read_required_line(in), "original_end"));
        b.display_pitch_midi = std::stod(prefix_value(read_required_line(in), "display_pitch_midi"));
        b.pitch_offset_semitones = std::stod(prefix_value(read_required_line(in), "pitch_offset_semitones"));
        b.time_stretch_ratio = std::stod(prefix_value(read_required_line(in), "time_stretch_ratio"));
        b.loudness_gain_db = std::stod(prefix_value(read_required_line(in), "loudness_gain_db"));
        b.detached = parse_bool_token(prefix_value(read_required_line(in), "detached"));
        b.edit_revision = static_cast<std::uint64_t>(std::stoull(prefix_value(read_required_line(in), "edit_revision")));

        const auto prev = std::stoll(prefix_value(read_required_line(in), "link_prev"));
        const auto next = std::stoll(prefix_value(read_required_line(in), "link_next"));
        if (prev >= 0) {
            b.link_prev = prev;
        }
        if (next >= 0) {
            b.link_next = next;
        }

        const auto op_count = static_cast<std::size_t>(std::stoull(prefix_value(read_required_line(in), "original_pitch_count")));
        b.original_pitch_slice.reserve(op_count);
        for (std::size_t j = 0; j < op_count; ++j) {
            b.original_pitch_slice.push_back(parse_pitch_line(read_required_line(in), "op "));
        }

        const auto ep_count = static_cast<std::size_t>(std::stoull(prefix_value(read_required_line(in), "edited_pitch_count")));
        b.edited_pitch_slice.reserve(ep_count);
        for (std::size_t j = 0; j < ep_count; ++j) {
            b.edited_pitch_slice.push_back(parse_pitch_line(read_required_line(in), "ep "));
        }

        if (read_required_line(in) != "blob_end") {
            throw std::runtime_error("project parse error: missing blob_end");
        }
        out.blobs.push_back(std::move(b));
    }

    return out;
}

} // namespace melodick::project
