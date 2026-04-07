#include "melodick/app/session.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace melodick::app {

namespace {

std::vector<float> slice_audio_range(const std::vector<float>& mono_samples, int sample_rate, const core::TimeRange& range) {
    if (sample_rate <= 0 || mono_samples.empty()) {
        return {};
    }
    const auto start = static_cast<std::size_t>(std::max(0.0, range.start_seconds) * sample_rate);
    const auto end = static_cast<std::size_t>(std::max(range.start_seconds, range.end_seconds) * sample_rate);
    if (start >= mono_samples.size()) {
        return {};
    }
    const auto clamped_end = std::min<std::size_t>(end, mono_samples.size());
    if (clamped_end <= start) {
        return {};
    }
    return std::vector(
        mono_samples.begin() + static_cast<std::ptrdiff_t>(start),
        mono_samples.begin() + static_cast<std::ptrdiff_t>(clamped_end));
}

std::vector<float> resample_linear_to_size(const std::vector<float>& input, std::size_t target_size) {
    if (target_size == 0) {
        return {};
    }
    if (input.empty()) {
        return std::vector(target_size, 0.0f);
    }
    if (input.size() == target_size) {
        return input;
    }
    if (target_size == 1) {
        return {input.front()};
    }

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

void apply_gain_db(std::vector<float>& audio, double gain_db) {
    if (std::fabs(gain_db) <= 1.0e-9) {
        return;
    }
    const auto gain = static_cast<float>(std::pow(10.0, gain_db / 20.0));
    for (auto& s : audio) s *= gain;
}

} // namespace

Session::Session(
    std::shared_ptr<capabilities::IPitchExtractor> pitch_extractor,
    std::shared_ptr<capabilities::IVocoder> vocoder,
    capabilities::SegmenterConfig segmenter_config,
    render::RenderGroupingConfig render_grouping)
    : chain_(std::move(pitch_extractor), std::move(vocoder), segmenter_config)
    , planner_(render_grouping) {
}

void Session::import_audio(const std::vector<float>& mono_samples, const int sample_rate) {
    if (sample_rate <= 0) {
        throw std::invalid_argument("sample_rate must be positive");
    }

    mono_samples_ = mono_samples;
    sample_rate_ = sample_rate;
    duration_seconds_ = mono_samples_.empty() ? 0.0 : (static_cast<double>(mono_samples_.size()) / sample_rate_);

    auto [f0, blobs] = chain_.analyze_and_segment(mono_samples_, sample_rate_);
    analysis_f0_ = std::move(f0);
    blobs_ = std::move(blobs);
    for (auto& blob : blobs_) if (blob.edit_revision == 0) blob.edit_revision = 1;
    render_units_ = planner_.plan(blobs_);
    rendered_blob_cache_.clear();
    refresh_derived_dirty();
}

const core::NoteBlob* Session::find_blob(const std::int64_t blob_id) const {
    for (const auto& blob : blobs_) if (blob.id == blob_id) return &blob;
    return nullptr;
}

core::NoteBlob* Session::find_blob(const std::int64_t blob_id) {
    for (auto& blob : blobs_) if (blob.id == blob_id) return &blob;
    return nullptr;
}

void Session::shift_blob_pitch(std::int64_t blob_id, double semitones) {
    auto* blob = find_blob(blob_id);
    if (!blob) return;
    core::NoteBlobOps::shift_pitch(*blob, semitones);
    render_units_ = planner_.plan(blobs_);
    refresh_derived_dirty();
}

void Session::stretch_blob_time(std::int64_t blob_id, double new_duration_seconds) {
    auto* blob = find_blob(blob_id);
    if (!blob) return;
    core::NoteBlobOps::stretch_time(*blob, new_duration_seconds);
    render_units_ = planner_.plan(blobs_);
    refresh_derived_dirty();
}

bool Session::is_unit_dirty(const render::RenderUnit& unit) const {
    for (const auto& blob_stub : unit.notes) {
        const auto* live = find_blob(blob_stub.id);
        if (!live) return true;

        const auto cache_it = rendered_blob_cache_.find(live->id);
        if (cache_it == rendered_blob_cache_.end()) return true;

        if (cache_it->second.source_revision != live->edit_revision) return true;
    }

    return false;
}

void Session::refresh_derived_dirty() {
    dirty_timeline_.reset(duration_seconds_);
    for (const auto& unit : render_units_) if (is_unit_dirty(unit)) dirty_timeline_.mark_dirty(unit.span);
}

std::vector<render::RenderUnit> Session::plan_render_from(double playhead_seconds, const std::size_t budget_units) const {
    return scheduler_.plan_from_playhead(playhead_seconds, render_units_, dirty_timeline_, budget_units);
}

void Session::render_units(const std::vector<render::RenderUnit>& units) {
    for (const auto& [span, notes] : units) {
        for (const auto& blob_stub : notes) {
            const auto* live_blob = find_blob(blob_stub.id);
            if (!live_blob) continue;

            core::TimeRange source_range {
                .start_seconds = live_blob->original_start_seconds,
                .end_seconds = live_blob->original_end_seconds,
            };

            if (!source_range.is_valid() || source_range.length() <= 0.0) source_range = live_blob->time;
            auto source = slice_audio_range(mono_samples_, sample_rate_, source_range);
            const auto target_samples = static_cast<std::size_t>(std::max(0.0, live_blob->duration()) * sample_rate_);

            std::vector<float> rendered {};
            if (live_blob->is_unedited()) rendered = std::move(source);
            else rendered = chain_.resynthesize_blob(*live_blob, source, sample_rate_);

            if (target_samples > 0 && rendered.size() != target_samples) rendered = resample_linear_to_size(rendered, target_samples);

            apply_gain_db(rendered, live_blob->loudness_gain_db);
            rendered_blob_cache_[live_blob->id] = RenderedBlobCache {
                .audio = std::move(rendered),
                .source_revision = live_blob->edit_revision,
            };
        }
    }
    refresh_derived_dirty();
}

std::vector<float> Session::build_rendered_mixdown() const {
    if (sample_rate_ <= 0) return {};

    std::size_t output_size = mono_samples_.size();
    for (const auto& blob : blobs_) {
        const auto it = rendered_blob_cache_.find(blob.id);
        if (it == rendered_blob_cache_.end()) continue;
        const auto start = static_cast<std::size_t>(std::max(0.0, blob.time.start_seconds) * sample_rate_);
        output_size = std::max(output_size, start + it->second.audio.size());
    }

    std::vector mixed(output_size, 0.0f);
    for (const auto& blob : blobs_) {
        const auto it = rendered_blob_cache_.find(blob.id);
        if (it == rendered_blob_cache_.end()) continue;

        const auto start = static_cast<std::size_t>(std::max(0.0, blob.time.start_seconds) * sample_rate_);
        const auto& clip = it->second.audio;
        for (std::size_t i = 0; i < clip.size(); ++i) {
            const auto out_index = start + i;
            if (out_index >= mixed.size()) break;
            mixed[out_index] += clip[i];
        }
    }

    for (auto& s : mixed) s = std::clamp(s, -1.0f, 1.0f);
    return mixed;
}

project::ProjectState Session::capture_project_state(const std::string& source_audio_path) const {
    project::ProjectState out {};
    out.sample_rate = sample_rate_;
    out.duration_seconds = duration_seconds_;
    out.source_audio_path = source_audio_path;
    out.analysis_f0 = analysis_f0_;
    out.blobs = blobs_;
    return out;
}

void Session::restore_project_state(const project::ProjectState& state, const std::vector<float>& mono_samples, const int sample_rate) {
    if (sample_rate <= 0) throw std::invalid_argument("sample_rate must be positive");

    mono_samples_ = mono_samples;
    sample_rate_ = sample_rate;
    duration_seconds_ = mono_samples_.empty() ? state.duration_seconds : (static_cast<double>(mono_samples_.size()) / sample_rate_);
    analysis_f0_ = state.analysis_f0;
    blobs_ = state.blobs;
    for (auto& blob : blobs_) if (blob.edit_revision == 0) blob.edit_revision = 1;

    render_units_ = planner_.plan(blobs_);
    rendered_blob_cache_.clear();
    refresh_derived_dirty();
}

void Session::render_all_dirty(const std::size_t budget_units) {
    if (budget_units == 0) return;

    std::size_t stagnant_rounds = 0;
    while (!dirty_timeline_.dirty_ranges().empty()) {
        const auto before = dirty_timeline_.dirty_ranges().size();
        const auto playhead = dirty_timeline_.dirty_ranges().front().start_seconds;
        auto units = plan_render_from(playhead, budget_units);
        if (units.empty()) break;
        render_units(units);

        const auto after = dirty_timeline_.dirty_ranges().size();
        if (after >= before) {
            ++stagnant_rounds;
            if (stagnant_rounds >= 4) throw std::runtime_error("render_all_dirty stalled: dirty ranges not shrinking");
        } else stagnant_rounds = 0;
    }
}

} // namespace melodick::app
