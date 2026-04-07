#include "melodick/io/wav_io.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace melodick::io {

namespace {

std::uint16_t read_u16_le(std::istream& in) {
    std::array<unsigned char, 2> b {};
    in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (!in) {
        throw std::runtime_error("failed to read u16");
    }
    return static_cast<std::uint16_t>(b[0] | (static_cast<std::uint16_t>(b[1]) << 8));
}

std::uint32_t read_u32_le(std::istream& in) {
    std::array<unsigned char, 4> b {};
    in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (!in) {
        throw std::runtime_error("failed to read u32");
    }
    return static_cast<std::uint32_t>(b[0])
        | (static_cast<std::uint32_t>(b[1]) << 8)
        | (static_cast<std::uint32_t>(b[2]) << 16)
        | (static_cast<std::uint32_t>(b[3]) << 24);
}

void write_u16_le(std::ostream& out, std::uint16_t value) {
    const std::array b {
        static_cast<unsigned char>(value & 0xff),
        static_cast<unsigned char>((value >> 8) & 0xff),
    };
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

void write_u32_le(std::ostream& out, std::uint32_t value) {
    const std::array b {
        static_cast<unsigned char>(value & 0xff),
        static_cast<unsigned char>((value >> 8) & 0xff),
        static_cast<unsigned char>((value >> 16) & 0xff),
        static_cast<unsigned char>((value >> 24) & 0xff),
    };
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

float decode_pcm24(const unsigned char* p) {
    std::int32_t v = static_cast<std::int32_t>(p[0])
        | (static_cast<std::int32_t>(p[1]) << 8)
        | (static_cast<std::int32_t>(p[2]) << 16);
    if (v & 0x00800000) {
        v |= 0xff000000;
    }
    return static_cast<float>(v) / 8388608.0f;
}

} // namespace

WavData read_wav_mono(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open wav: " + path);
    }

    std::array<char, 4> riff {};
    std::array<char, 4> wave {};
    in.read(riff.data(), static_cast<std::streamsize>(riff.size()));
    (void)read_u32_le(in);
    in.read(wave.data(), static_cast<std::streamsize>(wave.size()));
    if (!in || std::strncmp(riff.data(), "RIFF", 4) != 0 || std::strncmp(wave.data(), "WAVE", 4) != 0) {
        throw std::runtime_error("invalid RIFF/WAVE header");
    }

    std::uint16_t audio_format = 0;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits_per_sample = 0;
    std::uint16_t block_align = 0;

    std::vector<unsigned char> data_bytes {};
    bool have_fmt = false;
    bool have_data = false;

    while (in && !have_data) {
        std::array<char, 4> chunk_id {};
        in.read(chunk_id.data(), static_cast<std::streamsize>(chunk_id.size()));
        if (!in) {
            break;
        }
        const auto chunk_size = read_u32_le(in);

        if (std::strncmp(chunk_id.data(), "fmt ", 4) == 0) {
            audio_format = read_u16_le(in);
            channels = read_u16_le(in);
            sample_rate = read_u32_le(in);
            (void)read_u32_le(in); // byte rate
            block_align = read_u16_le(in);
            bits_per_sample = read_u16_le(in);

            const std::uint32_t base_fmt_size = 16;
            if (chunk_size > base_fmt_size) {
                in.seekg(static_cast<std::streamoff>(chunk_size - base_fmt_size), std::ios::cur);
            }
            have_fmt = true;
        } else if (std::strncmp(chunk_id.data(), "data", 4) == 0) {
            data_bytes.resize(chunk_size);
            if (!data_bytes.empty()) {
                in.read(reinterpret_cast<char*>(data_bytes.data()), static_cast<std::streamsize>(data_bytes.size()));
                if (!in) {
                    throw std::runtime_error("failed to read wav data chunk");
                }
            }
            have_data = true;
        } else {
            in.seekg(static_cast<std::streamoff>(chunk_size), std::ios::cur);
        }

        if (chunk_size % 2 == 1) {
            in.seekg(1, std::ios::cur);
        }
    }

    if (!have_fmt || !have_data) {
        throw std::runtime_error("wav missing fmt or data chunk");
    }
    if (channels == 0 || sample_rate == 0 || block_align == 0) {
        throw std::runtime_error("invalid wav format metadata");
    }
    if (audio_format != 1 && audio_format != 3) {
        throw std::runtime_error("unsupported wav format (only PCM/IEEE float)");
    }

    const std::size_t frame_count = data_bytes.size() / block_align;
    std::vector<float> mono {};
    mono.reserve(frame_count);

    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const auto* frame_ptr = data_bytes.data() + frame * block_align;
        double acc = 0.0;

        for (std::uint16_t ch = 0; ch < channels; ++ch) {
            const auto* s = frame_ptr + ch * (bits_per_sample / 8);
            float sample = 0.0f;

            if (audio_format == 1) {
                if (bits_per_sample == 16) {
                    std::int16_t v = 0;
                    std::memcpy(&v, s, sizeof(v));
                    sample = static_cast<float>(v) / 32768.0f;
                } else if (bits_per_sample == 24) {
                    sample = decode_pcm24(s);
                } else if (bits_per_sample == 32) {
                    std::int32_t v = 0;
                    std::memcpy(&v, s, sizeof(v));
                    sample = static_cast<float>(v) / 2147483648.0f;
                } else {
                    throw std::runtime_error("unsupported PCM bits per sample");
                }
            } else { // IEEE float
                if (bits_per_sample != 32) {
                    throw std::runtime_error("unsupported float bits per sample");
                }
                std::memcpy(&sample, s, sizeof(sample));
            }

            acc += static_cast<double>(sample);
        }

        mono.push_back(static_cast<float>(acc / static_cast<double>(channels)));
    }

    return WavData {.sample_rate = static_cast<int>(sample_rate), .mono_samples = std::move(mono)};
}

void write_wav_mono_16(const std::string& path, const std::vector<float>& mono_samples, int sample_rate) {
    if (sample_rate <= 0) {
        throw std::invalid_argument("sample_rate must be positive");
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open output wav: " + path);
    }

    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bits = 16;
    const std::uint32_t byte_rate = static_cast<std::uint32_t>(sample_rate) * channels * (bits / 8);
    const std::uint16_t block_align = channels * (bits / 8);
    const std::uint32_t data_size = static_cast<std::uint32_t>(mono_samples.size() * (bits / 8));
    const std::uint32_t riff_size = 4 + (8 + 16) + (8 + data_size);

    out.write("RIFF", 4);
    write_u32_le(out, riff_size);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    write_u32_le(out, 16);
    write_u16_le(out, 1); // PCM
    write_u16_le(out, channels);
    write_u32_le(out, static_cast<std::uint32_t>(sample_rate));
    write_u32_le(out, byte_rate);
    write_u16_le(out, block_align);
    write_u16_le(out, bits);

    out.write("data", 4);
    write_u32_le(out, data_size);

    for (float v : mono_samples) {
        const float clamped = std::clamp(v, -1.0f, 1.0f);
        const auto i16 = static_cast<std::int16_t>(std::lrint(clamped * 32767.0f));
        out.write(reinterpret_cast<const char*>(&i16), sizeof(i16));
    }

    if (!out) {
        throw std::runtime_error("failed while writing wav");
    }
}

} // namespace melodick::io
