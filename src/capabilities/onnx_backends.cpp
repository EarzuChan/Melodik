#include "melodick/capabilities/backends.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cctype>
#include <filesystem>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <onnxruntime_cxx_api.h>

namespace melodick::capabilities {

namespace {

constexpr int kRmvpeSampleRate = 16000;
constexpr int kRmvpeHop = 160;
constexpr int kModelSampleRate = 44100;
constexpr int kVocoderHop = 512;
constexpr int kMelBins = 128;
constexpr int kNfft = 2048;
constexpr float kEps = 1.0e-5f;

float hz_to_midi(const float hz) {
    if (hz <= 0.0f) return 0.0f;
    return 69.0f + 12.0f * std::log2(hz / 440.0f);
}

float midi_to_hz(const float midi) {
    if (midi <= 0.0f) return 0.0f;
    return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
}

float hz_to_mel(const float hz) {
    constexpr float kFSp = 200.0f / 3.0f;
    constexpr float kMinLogHz = 1000.0f;
    constexpr float kMinLogMel = kMinLogHz / kFSp;
    const float kLogStep = std::log(6.4f) / 27.0f;
    if (hz < kMinLogHz) return hz / kFSp;
    return kMinLogMel + std::log(hz / kMinLogHz) / kLogStep;
}

float mel_to_hz(const float mel) {
    constexpr float kFSp = 200.0f / 3.0f;
    constexpr float kMinLogHz = 1000.0f;
    constexpr float kMinLogMel = kMinLogHz / kFSp;
    const float kLogStep = std::log(6.4f) / 27.0f;
    if (mel < kMinLogMel) return kFSp * mel;
    return kMinLogHz * std::exp(kLogStep * (mel - kMinLogMel));
}

std::string lower_copy(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    return value;
}

int reflect_index(int i, const int n) {
    if (n <= 1) return 0;

    while (i < 0 || i >= n) {
        if (i < 0) i = -i;

        if (i >= n) i = 2 * n - i - 2;
    }

    return i;
}

std::vector<float> resample_linear(const std::vector<float>& input, const int src_rate, const int dst_rate) {
    if (src_rate <= 0 || dst_rate <= 0) throw std::invalid_argument("invalid sample rate");
    if (input.empty() || src_rate == dst_rate) return input;

    const double ratio = static_cast<double>(dst_rate) / static_cast<double>(src_rate);
    const std::size_t output_size = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(input.size() * ratio)));
    std::vector output(output_size, 0.0f);
    const double max_src_pos = static_cast<double>(input.size() - 1);

    for (std::size_t i = 0; i < output_size; ++i) {
        const double src_pos = std::clamp(static_cast<double>(i) / ratio, 0.0, max_src_pos);
        const auto idx0 = static_cast<std::size_t>(std::floor(src_pos));
        const auto idx1 = std::min<std::size_t>(idx0 + 1, input.size() - 1);
        const auto frac = static_cast<float>(src_pos - static_cast<double>(idx0));
        output[i] = input[idx0] * (1.0f - frac) + input[idx1] * frac;
    }
    return output;
}

void fft_inplace(std::vector<std::complex<float>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; i++) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const float ang = 2.0f * std::numbers::pi_v<float> / static_cast<float>(len) * 1.0f;
        const std::complex wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex w(1.0f, 0.0f);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

std::vector<std::vector<float>> build_mel_filterbank(const int sample_rate, const int n_fft, const int n_mels, const float f_min, const float f_max) {
    const int n_freqs = n_fft / 2 + 1;
    std::vector bank(static_cast<std::size_t>(n_mels), std::vector(static_cast<std::size_t>(n_freqs), 0.0f));

    const float mel_min = hz_to_mel(f_min);
    const float mel_max = hz_to_mel(std::min(f_max, sample_rate * 0.5f));

    std::vector mel_points(static_cast<std::size_t>(n_mels + 2), 0.0f);
    std::vector hz_points(static_cast<std::size_t>(n_mels + 2), 0.0f);
    std::vector bins(static_cast<std::size_t>(n_mels + 2), 0);
    for (int i = 0; i < n_mels + 2; ++i) {
        mel_points[static_cast<std::size_t>(i)] = mel_min + (mel_max - mel_min) * (static_cast<float>(i) / static_cast<float>(n_mels + 1));
        hz_points[static_cast<std::size_t>(i)] = mel_to_hz(mel_points[static_cast<std::size_t>(i)]);
        bins[static_cast<std::size_t>(i)] = static_cast<int>(std::floor((1 + n_fft) * hz_points[static_cast<std::size_t>(i)] / sample_rate));
        bins[static_cast<std::size_t>(i)] = std::clamp(bins[static_cast<std::size_t>(i)], 0, n_freqs - 1);
    }

    for (int m = 0; m < n_mels; ++m) {
        const int left = bins[static_cast<std::size_t>(m)];
        const int center = bins[static_cast<std::size_t>(m + 1)];
        const int right = bins[static_cast<std::size_t>(m + 2)];

        if (center <= left || right <= center) continue;
        for (int k = left; k < center; ++k) bank[static_cast<std::size_t>(m)][static_cast<std::size_t>(k)] = static_cast<float>(k - left) / static_cast<float>(center - left);
        for (int k = center; k < right; ++k) bank[static_cast<std::size_t>(m)][static_cast<std::size_t>(k)] = static_cast<float>(right - k) / static_cast<float>(right - center);

        const float hz_left = hz_points[static_cast<std::size_t>(m)];
        const float hz_right = hz_points[static_cast<std::size_t>(m + 2)];
        const float norm = hz_right > hz_left ? 2.0f / (hz_right - hz_left) : 1.0f;

        for (int k = 0; k < n_freqs; ++k) bank[static_cast<std::size_t>(m)][static_cast<std::size_t>(k)] *= norm;
    }

    return bank;
}

struct MelResult {
    std::vector<float> mel; // [mel_bins, frames]
    std::size_t frames {0};
};

struct PitchTrackResult {
    std::vector<float> f0_hz {};
    std::vector<float> uv {};
};

void fill_small_f0_gaps(std::vector<float>& f0, std::size_t max_gap_frames);

MelResult compute_log_mel(const std::vector<float>& audio, const int sample_rate) {
    if (audio.empty()) return {};

    constexpr int n_fft = kNfft;
    constexpr int hop = kVocoderHop;
    constexpr int n_freqs = n_fft / 2 + 1;
    constexpr int n_mels = kMelBins;
    const auto filterbank = build_mel_filterbank(sample_rate, n_fft, n_mels, 40.0f, 16000.0f);

    std::vector window(n_fft, 0.0f);
    for (int i = 0; i < n_fft; ++i) {
        constexpr float denom = static_cast<float>(std::max(1, n_fft - 1));
        window[static_cast<std::size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / denom);
    }

    const std::size_t frames = std::max<std::size_t>(1, (audio.size() + static_cast<std::size_t>(hop) - 1) / static_cast<std::size_t>(hop));
    std::vector mel(static_cast<std::size_t>(n_mels) * frames, 0.0f);

    std::vector<std::complex<float>> fft_buf(n_fft);
    std::vector magnitude(n_freqs, 0.0f);

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const int center = static_cast<int>(frame * static_cast<std::size_t>(hop));
        const int start = center - n_fft / 2;

        for (int i = 0; i < n_fft; ++i) {
            const int src = reflect_index(start + i, static_cast<int>(audio.size()));
            fft_buf[static_cast<std::size_t>(i)] = std::complex(audio[static_cast<std::size_t>(src)] * window[static_cast<std::size_t>(i)], 0.0f);
        }

        fft_inplace(fft_buf);
        for (int k = 0; k < n_freqs; ++k) {
            const auto c = fft_buf[static_cast<std::size_t>(k)];
            magnitude[static_cast<std::size_t>(k)] = std::sqrt((c.real() * c.real()) + (c.imag() * c.imag()));
        }

        for (int m = 0; m < n_mels; ++m) {
            float sum = 0.0f;
            const auto& w = filterbank[static_cast<std::size_t>(m)];
            for (int k = 0; k < n_freqs; ++k) sum += magnitude[static_cast<std::size_t>(k)] * w[static_cast<std::size_t>(k)];
            mel[static_cast<std::size_t>(m) * frames + frame] = std::log(std::max(kEps, sum));
        }
    }

    return MelResult {.mel = std::move(mel), .frames = frames};
}

std::vector<core::PitchPoint> collect_group_pitch_points(const std::vector<core::NoteBlob>& notes) {
    std::vector<core::PitchPoint> points {};
    for (const auto& note : notes) {
        const auto final_curve = note.final_pitch_curve();
        if (!final_curve.empty()) {
            points.insert(points.end(), final_curve.begin(), final_curve.end());
            continue;
        }

        core::PitchPoint p0 {};
        p0.seconds = note.time.start_seconds;
        p0.midi = note.final_display_pitch_midi();
        p0.voiced = p0.midi > 0.0;
        p0.confidence = 1.0f;

        core::PitchPoint p1 = p0;
        p1.seconds = note.time.end_seconds;
        points.push_back(p0);
        points.push_back(p1);
    }

    std::sort(points.begin(), points.end(), [](const core::PitchPoint& a, const core::PitchPoint& b) {
        return a.seconds < b.seconds;
    });
    return points;
}

float pitch_point_voiced_probability(const core::PitchPoint& point) {
    if (!point.voiced) {
        return 0.0f;
    }
    return std::clamp(point.confidence, 0.0f, 1.0f);
}

PitchTrackResult interpolate_points_pitch_track(
    const std::vector<core::PitchPoint>& points,
    const std::size_t frames,
    const double timeline_start_seconds,
    const int sample_rate,
    const float uv_threshold) {
    PitchTrackResult out {};
    out.f0_hz.assign(frames, 0.0f);
    out.uv.assign(frames, 0.0f);
    if (frames == 0 || points.empty()) {
        return out;
    }

    std::size_t j = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = timeline_start_seconds
            + static_cast<double>(i * static_cast<std::size_t>(kVocoderHop)) / static_cast<double>(sample_rate);
        while (j + 1 < points.size() && points[j + 1].seconds < t) {
            ++j;
        }

        const auto& p0 = points[j];
        const auto p0_uv = pitch_point_voiced_probability(p0);

        if (j + 1 >= points.size()) {
            out.uv[i] = p0_uv;
            if (p0.voiced) {
                out.f0_hz[i] = midi_to_hz(static_cast<float>(p0.midi));
            }
            continue;
        }

        const auto& p1 = points[j + 1];
        const auto p1_uv = pitch_point_voiced_probability(p1);

        const double dt = p1.seconds - p0.seconds;
        if (dt <= 1.0e-9) {
            out.uv[i] = p0_uv;
            if (p0.voiced) {
                out.f0_hz[i] = midi_to_hz(static_cast<float>(p0.midi));
            }
            continue;
        }

        const float alpha = static_cast<float>((t - p0.seconds) / dt);
        const auto clamped_alpha = std::clamp(alpha, 0.0f, 1.0f);
        out.uv[i] = p0_uv + (p1_uv - p0_uv) * clamped_alpha;
        if (!(p0.voiced && p1.voiced)) {
            continue;
        }

        const float midi = static_cast<float>(p0.midi)
            + (static_cast<float>(p1.midi) - static_cast<float>(p0.midi)) * clamped_alpha;
        out.f0_hz[i] = midi_to_hz(midi);
    }

    fill_small_f0_gaps(out.f0_hz, 8);
    for (std::size_t i = 0; i < frames; ++i) {
        if (out.f0_hz[i] <= 0.0f) {
            out.uv[i] = 0.0f;
            continue;
        }
        out.uv[i] = std::clamp(std::max(out.uv[i], uv_threshold), 0.0f, 1.0f);
    }
    return out;
}

PitchTrackResult assemble_group_source_pitch_track(
    const std::vector<core::NoteBlob>& notes,
    const std::size_t frames,
    const double timeline_start_seconds,
    const int sample_rate,
    const float uv_threshold) {
    PitchTrackResult out {};
    out.f0_hz.assign(frames, 0.0f);
    out.uv.assign(frames, 0.0f);
    if (frames == 0) {
        return out;
    }

    for (std::size_t i = 0; i < frames; ++i) {
        const double t = timeline_start_seconds
            + static_cast<double>(i * static_cast<std::size_t>(kVocoderHop)) / static_cast<double>(sample_rate);
        float uv = 0.0f;
        float f0_hz = 0.0f;
        for (const auto& note : notes) {
            const double duration = note.current_duration_seconds();
            if (duration <= 0.0 || t < note.time.start_seconds || t > note.time.end_seconds) {
                continue;
            }

            const double local_u = (t - note.time.start_seconds) / duration;
            const auto note_uv = note.sample_source_voiced_probability(local_u);
            uv = std::max(uv, note_uv);

            const auto base_f0_hz = note.sample_source_f0_hz(local_u);
            if (base_f0_hz <= 0.0f) {
                continue;
            }

            const auto delta_midi = note.sample_pitch_delta_midi(local_u);
            const auto scale = static_cast<float>(std::pow(2.0, delta_midi / 12.0));
            f0_hz = std::max(f0_hz, base_f0_hz * scale);
        }

        out.f0_hz[i] = f0_hz;
        out.uv[i] = std::clamp(uv, 0.0f, 1.0f);
    }

    fill_small_f0_gaps(out.f0_hz, 8);
    for (std::size_t i = 0; i < frames; ++i) {
        if (out.f0_hz[i] <= 0.0f) {
            out.uv[i] = 0.0f;
            continue;
        }
        out.uv[i] = std::clamp(std::max(out.uv[i], uv_threshold), 0.0f, 1.0f);
    }

    return out;
}

std::vector<float> assemble_group_source_uv(
    const std::vector<core::NoteBlob>& notes,
    const std::size_t frames,
    const double timeline_start_seconds,
    const int sample_rate) {
    std::vector<float> out(frames, 0.0f);
    if (frames == 0) {
        return out;
    }

    for (std::size_t i = 0; i < frames; ++i) {
        const double t = timeline_start_seconds
            + static_cast<double>(i * static_cast<std::size_t>(kVocoderHop)) / static_cast<double>(sample_rate);
        float uv = 0.0f;
        for (const auto& note : notes) {
            const double duration = note.current_duration_seconds();
            if (duration <= 0.0 || t < note.time.start_seconds || t > note.time.end_seconds) {
                continue;
            }

            const double local_u = (t - note.time.start_seconds) / duration;
            uv = std::max(uv, note.sample_source_voiced_probability(local_u));
        }
        out[i] = std::clamp(uv, 0.0f, 1.0f);
    }

    return out;
}

core::TimeRange group_time_span(const std::vector<core::NoteBlob>& notes) {
    core::TimeRange span {};
    if (notes.empty()) {
        return span;
    }
    span.start_seconds = notes.front().time.start_seconds;
    span.end_seconds = notes.front().time.end_seconds;
    for (const auto& note : notes) {
        span.start_seconds = std::min(span.start_seconds, note.time.start_seconds);
        span.end_seconds = std::max(span.end_seconds, note.time.end_seconds);
    }
    return span;
}

std::vector<float> assemble_group_source_audio(
    const std::vector<core::NoteBlob>& notes,
    const core::TimeRange& span,
    const int sample_rate) {
    const auto target_samples = static_cast<std::size_t>(
        std::max(0.0, span.end_seconds - span.start_seconds) * static_cast<double>(sample_rate));
    if (target_samples == 0) {
        return {};
    }

    std::vector<float> out(target_samples, 0.0f);
    for (const auto& note : notes) {
        if (note.source_audio_44k.empty()) {
            continue;
        }
        const auto source = sample_rate == kModelSampleRate
            ? note.source_audio_44k
            : resample_linear(note.source_audio_44k, kModelSampleRate, sample_rate);

        const auto offset = static_cast<std::size_t>(
            std::max(0.0, note.time.start_seconds - span.start_seconds) * static_cast<double>(sample_rate));
        for (std::size_t i = 0; i < source.size(); ++i) {
            const auto out_index = offset + i;
            if (out_index >= out.size()) {
                break;
            }
            out[out_index] += source[i];
        }
    }

    for (auto& s : out) {
        s = std::clamp(s, -1.0f, 1.0f);
    }
    return out;
}

void fill_small_f0_gaps(std::vector<float>& f0, const std::size_t max_gap_frames) {
    if (f0.empty() || max_gap_frames == 0) return;

    std::size_t i = 0;
    while (i < f0.size()) {
        while (i < f0.size() && f0[i] > 0.0f) ++i;
        const std::size_t gap_start = i;
        while (i < f0.size() && f0[i] <= 0.0f) ++i;
        const std::size_t gap_end = i;
        const std::size_t gap_len = gap_end - gap_start;
        if (gap_len == 0 || gap_len > max_gap_frames) continue;
        if (gap_start == 0 || gap_end >= f0.size()) continue;

        const float left = f0[gap_start - 1];
        const float right = f0[gap_end];
        if (left <= 0.0f || right <= 0.0f) continue;

        const float log_left = std::log2(std::max(left, 1.0e-6f));
        const float log_right = std::log2(std::max(right, 1.0e-6f));
        for (std::size_t j = 0; j < gap_len; ++j) {
            const float t = static_cast<float>(j + 1) / static_cast<float>(gap_len + 1);
            f0[gap_start + j] = std::pow(2.0f, log_left + (log_right - log_left) * t);
        }
    }
}

bool can_use_cached_note_mel(const core::NoteBlob& note, const std::vector<float>& source_model) {
    if (note.cached_source_mel_bins != kMelBins || note.cached_source_mel_frames <= 0) {
        return false;
    }
    if (note.cached_source_mel_log.size() != static_cast<std::size_t>(kMelBins) * static_cast<std::size_t>(note.cached_source_mel_frames)) {
        return false;
    }
    if (source_model.size() != note.source_audio_44k.size()) {
        return false;
    }
    return true;
}

std::vector<float> resample_to_length(const std::vector<float>& input, const std::size_t target_size) {
    if (target_size == 0) return {};
    if (input.empty()) return std::vector(target_size, 0.0f);
    if (input.size() == target_size) return input;
    if (target_size == 1) return {input.front()};

    std::vector out(target_size, 0.0f);
    const double scale = static_cast<double>(input.size() - 1) / static_cast<double>(target_size - 1);
    for (std::size_t i = 0; i < target_size; ++i) {
        const double pos = static_cast<double>(i) * scale;
        const auto i0 = static_cast<std::size_t>(std::floor(pos));
        const auto i1 = std::min<std::size_t>(i0 + 1, input.size() - 1);
        const auto frac = static_cast<float>(pos - static_cast<double>(i0));
        out[i] = input[i0] * (1.0f - frac) + input[i1] * frac;
    }
    return out;
}

class OnnxPitchExtractor final : public IPitchExtractor {
public:
    explicit OnnxPitchExtractor(BackendConfig config)
        : config_(std::move(config))
        , env_(ORT_LOGGING_LEVEL_WARNING, "melodick-rmvpe")
        , memory_info_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {

        if (!std::filesystem::exists(config_.rmvpe_model_path)) throw std::runtime_error("RMVPE model not found: " + config_.rmvpe_model_path);

        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(std::max(1, config_.inference_threads));
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#if defined(_WIN32)
        const auto model_w = std::filesystem::path(config_.rmvpe_model_path).wstring();
        session_ = std::make_unique<Ort::Session>(env_, model_w.c_str(), options);
#else
        session_ = std::make_unique<Ort::Session>(env_, config_.rmvpe_model_path.c_str(), options);
#endif
        if (!session_) throw std::runtime_error("Failed to create RMVPE ONNX session");

        Ort::AllocatorWithDefaultOptions allocator;
        const std::size_t input_count = session_->GetInputCount();
        for (std::size_t i = 0; i < input_count; ++i) {
            auto name = session_->GetInputNameAllocated(i, allocator);
            input_names_.emplace_back(name.get());
        }
        const std::size_t output_count = session_->GetOutputCount();
        for (std::size_t i = 0; i < output_count; ++i) {
            auto name = session_->GetOutputNameAllocated(i, allocator);
            output_names_.emplace_back(name.get());
        }
    }

    core::PitchSlice extract_f0(const std::vector<float>& mono_samples, const int sample_rate) override {
        if (mono_samples.empty()) return {};
        if (sample_rate <= 0) throw std::invalid_argument("sample_rate must be positive");

        auto audio_16k = resample_linear(mono_samples, sample_rate, kRmvpeSampleRate);
        const std::vector waveform_shape {1, static_cast<int64_t>(audio_16k.size())};

        auto waveform = Ort::Value::CreateTensor<float>(
            memory_info_,
            audio_16k.data(),
            audio_16k.size(),
            waveform_shape.data(),
            waveform_shape.size());

        float threshold = config_.uv_threshold;
        const std::vector<int64_t> threshold_shape {1};
        auto threshold_tensor = Ort::Value::CreateTensor<float>(memory_info_, &threshold, 1, threshold_shape.data(), threshold_shape.size());

        const char* input_names[2] {"waveform", "threshold"};
        std::array inputs {std::move(waveform), std::move(threshold_tensor)};
        const char* output_names[2] {"f0", "uv"};
        const auto outputs = session_->Run(Ort::RunOptions {nullptr}, input_names, inputs.data(), inputs.size(), output_names, 2);

        if (outputs.size() < 2) throw std::runtime_error("RMVPE output missing");

        const auto f0_info = outputs[0].GetTensorTypeAndShapeInfo();
        const auto uv_info = outputs[1].GetTensorTypeAndShapeInfo();
        const auto f0_shape = f0_info.GetShape();
        const std::size_t frames = f0_shape.size() >= 2 ? static_cast<std::size_t>(f0_shape[1]) : f0_info.GetElementCount();
        if (frames == 0) return {};

        const auto* f0_data = outputs[0].GetTensorData<float>();
        const auto* uv_data = outputs[1].GetTensorData<float>();

        core::PitchSlice result {};
        result.reserve(frames);
        std::size_t plausible_count = 0;
        std::size_t voiced_if_ge = 0;
        std::size_t voiced_if_le = 0;
        for (std::size_t i = 0; i < frames; ++i) {
            const float f0_hz = f0_data[i];
            const float uv = std::clamp(uv_data[i], 0.0f, 1.0f);
            if (const bool plausible_f0 = (f0_hz >= 40.0f) && (f0_hz <= 1400.0f); !plausible_f0) continue;
            ++plausible_count;
            if (uv >= config_.uv_threshold) ++voiced_if_ge;
            if (uv <= config_.uv_threshold) ++voiced_if_le;
        }

        bool uv_is_unvoiced_probability = false;
        if (config_.enable_uv_check && plausible_count > 0) {
            const double ratio_ge = static_cast<double>(voiced_if_ge) / static_cast<double>(plausible_count);
            const double ratio_le = static_cast<double>(voiced_if_le) / static_cast<double>(plausible_count);
            // Most RMVPE exports use "voiced confidence". Some use "unvoiced probability".
            // Pick the polarity that avoids collapsing voiced ratio to near-zero.
            uv_is_unvoiced_probability = ratio_ge < 0.10 && ratio_le > ratio_ge * 1.8;
        }

        for (std::size_t i = 0; i < frames; ++i) {
            const float f0_hz = f0_data[i];
            const float uv = std::clamp(uv_data[i], 0.0f, 1.0f);
            const bool plausible_f0 = (f0_hz >= 40.0f) && (f0_hz <= 1400.0f);
            const float voiced_probability = plausible_f0
                ? (uv_is_unvoiced_probability ? (1.0f - uv) : uv)
                : 0.0f;
            // Preserve the extractor's usable F0 whenever it is plausible. UV is carried alongside
            // as an auxiliary confidence track and should not zero out the source pitch itself.
            const bool voiced = plausible_f0;
            result.push_back(core::PitchPoint {
                .seconds = static_cast<double>(i) * static_cast<double>(kRmvpeHop) / static_cast<double>(kRmvpeSampleRate),
                .midi = voiced ? hz_to_midi(f0_hz) : 0.0,
                .voiced = voiced,
                .confidence = voiced_probability,
            });
        }
        return result;
    }

private:
    BackendConfig config_;
    Ort::Env env_;
    Ort::MemoryInfo memory_info_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> input_names_ {};
    std::vector<std::string> output_names_ {};
};

class OnnxHifiganVocoder final : public IVocoder {
public:
    explicit OnnxHifiganVocoder(BackendConfig config)
        : config_(std::move(config))
        , env_(ORT_LOGGING_LEVEL_WARNING, "melodick-hifigan")
        , memory_info_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {

        if (!std::filesystem::exists(config_.hifigan_model_path)) throw std::runtime_error("HiFiGAN model not found: " + config_.hifigan_model_path);

        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(std::max(1, config_.inference_threads));
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#if defined(_WIN32)
        const auto model_w = std::filesystem::path(config_.hifigan_model_path).wstring();
        session_ = std::make_unique<Ort::Session>(env_, model_w.c_str(), options);
#else
        session_ = std::make_unique<Ort::Session>(env_, config_.hifigan_model_path.c_str(), options);
#endif

        Ort::AllocatorWithDefaultOptions allocator;
        const std::size_t input_count = session_->GetInputCount();
        input_names_.reserve(input_count);
        input_shapes_.reserve(input_count);
        input_types_.reserve(input_count);
        for (std::size_t i = 0; i < input_count; ++i) {
            auto name = session_->GetInputNameAllocated(i, allocator);
            input_names_.emplace_back(name.get());

            auto info = session_->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
            input_shapes_.push_back(info.GetShape());
            input_types_.push_back(info.GetElementType());
        }

        const std::size_t output_count = session_->GetOutputCount();
        for (std::size_t i = 0; i < output_count; ++i) {
            auto name = session_->GetOutputNameAllocated(i, allocator);
            output_names_.emplace_back(name.get());
        }

        for (std::size_t i = 0; i < input_names_.size(); ++i) {
            const auto lowered = lower_copy(input_names_[i]);
            if (mel_index_ < 0 && (lowered.find("mel") != std::string::npos || lowered == "c")) mel_index_ = static_cast<int>(i);
            if (f0_index_ < 0 && lowered.find("f0") != std::string::npos) f0_index_ = static_cast<int>(i);
            if (uv_index_ < 0 && (lowered.find("uv") != std::string::npos || lowered.find("voiced") != std::string::npos)) uv_index_ = static_cast<int>(i);
        }
        if (mel_index_ < 0 || f0_index_ < 0) throw std::runtime_error("HiFiGAN model missing required inputs (mel/f0)");
    }

    void prepare_blob(core::NoteBlob& note, const int sample_rate) override {
        (void)sample_rate;
        note.cached_source_mel_bins = 0;
        note.cached_source_mel_frames = 0;
        note.cached_source_mel_log.clear();
        if (note.source_audio_44k.empty()) {
            return;
        }

        const auto mel = compute_log_mel(note.source_audio_44k, kModelSampleRate);
        note.cached_source_mel_bins = kMelBins;
        note.cached_source_mel_frames = static_cast<int>(mel.frames);
        note.cached_source_mel_log = mel.mel;
    }

    std::vector<float> render_group_audio(const std::vector<core::NoteBlob>& notes, int sample_rate) override {
        if (notes.empty()) return {};
        if (sample_rate <= 0) throw std::invalid_argument("sample_rate must be positive");

        const auto span = group_time_span(notes);
        const auto target_samples = static_cast<std::size_t>(
            std::max(0.0, span.end_seconds - span.start_seconds) * static_cast<double>(sample_rate));
        if (target_samples == 0) {
            return {};
        }

        const auto source_group = assemble_group_source_audio(notes, span, sample_rate);
        if (source_group.empty()) {
            return std::vector<float>(target_samples, 0.0f);
        }

        auto source_model = sample_rate == kModelSampleRate
            ? source_group
            : resample_linear(source_group, sample_rate, kModelSampleRate);
        if (source_model.empty()) {
            return std::vector<float>(target_samples, 0.0f);
        }
        if (const auto model_target_samples = static_cast<std::size_t>(
                std::max(0.0, span.end_seconds - span.start_seconds) * static_cast<double>(kModelSampleRate));
            model_target_samples > 0 && source_model.size() != model_target_samples) {
            source_model = resample_to_length(source_model, model_target_samples);
        }

        MelResult mel_result {};
        if (notes.size() == 1 && can_use_cached_note_mel(notes.front(), source_model)) {
            mel_result.frames = static_cast<std::size_t>(notes.front().cached_source_mel_frames);
            mel_result.mel = notes.front().cached_source_mel_log;
        } else {
            mel_result = compute_log_mel(source_model, kModelSampleRate);
        }
        if (mel_result.frames == 0 || mel_result.mel.empty()) return {};

        const auto frames = mel_result.frames;
        const bool has_source_pitch = std::ranges::any_of(notes, [](const core::NoteBlob& note) {
            return !note.source_f0_hz.empty();
        });
        const auto points = has_source_pitch ? std::vector<core::PitchPoint> {} : collect_group_pitch_points(notes);
        const auto pitch_track = has_source_pitch
            ? assemble_group_source_pitch_track(notes, frames, span.start_seconds, kModelSampleRate, config_.uv_threshold)
            : interpolate_points_pitch_track(points, frames, span.start_seconds, kModelSampleRate, config_.uv_threshold);
        const auto& f0_hz = pitch_track.f0_hz;
        auto uv = pitch_track.uv;

        std::vector mel_tf(static_cast<std::size_t>(kMelBins) * frames, 0.0f);
        for (std::size_t t = 0; t < frames; ++t) for (int m = 0; m < kMelBins; ++m) mel_tf[t * static_cast<std::size_t>(kMelBins) + static_cast<std::size_t>(m)] = mel_result.mel[static_cast<std::size_t>(m) * frames + t];

        auto resolve_shape = [&](const std::vector<int64_t>& raw, bool is_mel) {
            if (raw.empty()) return std::vector<int64_t> {};

            std::vector<int64_t> out = raw;
            for (std::size_t d = 0; d < out.size(); ++d) {
                if (out[d] > 0) continue;

                if (d == 0) {
                    out[d] = 1;
                    continue;
                }

                if (!is_mel) {
                    out[d] = static_cast<int64_t>(frames);
                    continue;
                }

                if (out.size() == 3) {
                    if (d == 1) out[d] = raw[2] == kMelBins ? static_cast<int64_t>(frames) : kMelBins;
                    else out[d] = raw[1] == kMelBins ? static_cast<int64_t>(frames) : kMelBins;
                } else out[d] = static_cast<int64_t>(frames);
            }
            return out;
        };

        std::vector<std::string> input_name_storage {};
        std::vector<Ort::Value> input_tensors {};
        std::vector<std::vector<float>> float_storage {};
        std::vector<std::vector<int64_t>> int64_storage {};
        input_name_storage.reserve(input_names_.size());
        input_tensors.reserve(input_names_.size());
        float_storage.reserve(input_names_.size());
        int64_storage.reserve(input_names_.size());

        auto push_float_tensor = [&](const std::string& name, const float* data, const std::size_t count, const std::vector<int64_t>& shape) {
            input_name_storage.push_back(name);
            input_tensors.push_back(Ort::Value::CreateTensor<float>(memory_info_, const_cast<float*>(data), count, shape.data(), shape.size()));
        };

        for (std::size_t i = 0; i < input_names_.size(); ++i) {
            const auto shape = resolve_shape(input_shapes_[i], static_cast<int>(i) == mel_index_);

            if (static_cast<int>(i) == mel_index_) {
                bool expect_tf = false;
                if (shape.size() == 3 && shape[2] == kMelBins) expect_tf = true;
                if (shape.size() == 2 && shape[1] == kMelBins) expect_tf = true;
                const float* mel_ptr = expect_tf ? mel_tf.data() : mel_result.mel.data();
                push_float_tensor(input_names_[i], mel_ptr, mel_result.mel.size(), shape);
                continue;
            }

            if (static_cast<int>(i) == f0_index_) {
                push_float_tensor(input_names_[i], f0_hz.data(), f0_hz.size(), shape);
                continue;
            }

            if (static_cast<int>(i) == uv_index_) {
                push_float_tensor(input_names_[i], uv.data(), uv.size(), shape);
                continue;
            }

            std::size_t count = 1;
            for (auto d : shape) count *= static_cast<std::size_t>(std::max<int64_t>(1, d));

            if (input_types_[i] == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
                int64_storage.emplace_back(count, 0);
                input_name_storage.push_back(input_names_[i]);
                input_tensors.push_back(Ort::Value::CreateTensor<int64_t>(
                    memory_info_,
                    int64_storage.back().data(),
                    int64_storage.back().size(),
                    shape.data(),
                    shape.size()));
            } else {
                float_storage.emplace_back(count, 0.0f);
                push_float_tensor(input_names_[i], float_storage.back().data(), float_storage.back().size(), shape);
            }
        }

        std::vector<const char*> input_name_ptrs {};
        input_name_ptrs.reserve(input_name_storage.size());
        for (const auto& n : input_name_storage) input_name_ptrs.push_back(n.c_str());

        std::vector<const char*> output_names {};
        for (const auto& n : output_names_) output_names.push_back(n.c_str());
        if (output_names.empty()) output_names.push_back("audio");

        auto out = session_->Run(
            Ort::RunOptions {nullptr},
            input_name_ptrs.data(),
            input_tensors.data(),
            input_tensors.size(),
            output_names.data(),
            output_names.size());

        if (out.empty()) throw std::runtime_error("HiFiGAN output is empty");

        auto info = out[0].GetTensorTypeAndShapeInfo();
        const auto count = info.GetElementCount();
        if (count == 0) return {};
        const auto* data = out[0].GetTensorData<float>();
        std::vector rendered_model(data, data + count);
        if (sample_rate == kModelSampleRate) {
            return resample_to_length(rendered_model, target_samples);
        }

        auto rendered_target = resample_linear(rendered_model, kModelSampleRate, sample_rate);
        return resample_to_length(rendered_target, target_samples);
    }

private:
    BackendConfig config_;
    Ort::Env env_;
    Ort::MemoryInfo memory_info_;
    std::unique_ptr<Ort::Session> session_;

    std::vector<std::string> input_names_ {};
    std::vector<std::string> output_names_ {};
    std::vector<std::vector<int64_t>> input_shapes_ {};
    std::vector<ONNXTensorElementDataType> input_types_ {};
    int mel_index_ {-1};
    int f0_index_ {-1};
    int uv_index_ {-1};
};

} // namespace

BackendConfig default_backend_config() {
    const std::filesystem::path root = std::filesystem::path(MELODICK_SOURCE_DIR) / ".." / "MelodickOld";

    BackendConfig cfg {};
    cfg.rmvpe_model_path = (root / "models" / "rmvpe.onnx").string();

    const auto primary_hifigan = root / "models" / "hifigan.onnx";
    const auto fallback_hifigan = root / "pc_nsf_hifigan_44.1k_ONNX" / "pc_nsf_hifigan_44.1k_hop512_128bin_2025.02.onnx";
    cfg.hifigan_model_path = std::filesystem::exists(primary_hifigan) ? primary_hifigan.string() : fallback_hifigan.string();
    cfg.enable_uv_check = true;
    return cfg;
}

std::shared_ptr<IPitchExtractor> create_pitch_extractor(const BackendConfig& config) {
    return std::make_shared<OnnxPitchExtractor>(config);
}

std::shared_ptr<IVocoder> create_vocoder(const BackendConfig& config) {
    return std::make_shared<OnnxHifiganVocoder>(config);
}

} // namespace melodick::capabilities
