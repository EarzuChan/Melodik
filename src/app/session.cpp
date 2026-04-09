#include "melodick/app/session.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>

#include "melodick/io/wav_io.h"

namespace melodick::app {
    namespace {
        constexpr double kUnvoicedBlobFadeSeconds = 0.005;

        std::vector<float> downmix_interleaved_to_mono(const std::vector<float>& interleaved, const int channels) {
            if (channels <= 0) throw std::invalid_argument("channels must be positive");
            if (interleaved.empty()) return {};
            if (channels == 1) return interleaved;

            const std::size_t frames = interleaved.size() / static_cast<std::size_t>(channels);
            std::vector mono(frames, 0.0f);

            for (std::size_t frame = 0; frame < frames; ++frame) {
                const std::size_t base = frame * static_cast<std::size_t>(channels);
                double sum = 0.0;
                for (int ch = 0; ch < channels; ++ch) sum += static_cast<double>(interleaved[base + static_cast<std::size_t>(ch)]);
                mono[frame] = static_cast<float>(sum / static_cast<double>(channels));
            }

            return mono;
        }

        std::vector<float> resample_linear_rate(const std::vector<float>& input, const int src_rate, const int dst_rate) {
            if (src_rate <= 0 || dst_rate <= 0) throw std::invalid_argument("invalid sample rate");
            if (input.empty() || src_rate == dst_rate) return input;

            const double ratio = static_cast<double>(dst_rate) / static_cast<double>(src_rate);
            const std::size_t output_size = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(static_cast<double>(input.size()) * ratio)));
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

        std::vector<float> normalize_to_session_audio(const std::vector<float>& interleaved_samples, const int sample_rate, const int channels) {
            if (sample_rate <= 0) throw std::invalid_argument("sample_rate must be positive");
            if (channels <= 0) throw std::invalid_argument("channels must be positive");

            auto mono = downmix_interleaved_to_mono(interleaved_samples, channels);
            return sample_rate == kSessionSampleRate ? mono : resample_linear_rate(mono, sample_rate, kSessionSampleRate);
        }

        std::vector<float> slice_audio_range(const std::vector<float>& mono_samples, const int sample_rate, const core::TimeRange& range) {
            if (sample_rate <= 0 || mono_samples.empty()) { return {}; }

            const auto start = static_cast<std::size_t>(std::max(0.0, range.start_seconds) * static_cast<double>(sample_rate));
            const auto end = static_cast<std::size_t>(std::max(range.start_seconds, range.end_seconds) * static_cast<double>(sample_rate));
            if (start >= mono_samples.size()) { return {}; }
            const auto clamped_end = std::min<std::size_t>(end, mono_samples.size());
            if (clamped_end <= start) { return {}; }
            return std::vector(mono_samples.begin() + static_cast<std::ptrdiff_t>(start),mono_samples.begin() + static_cast<std::ptrdiff_t>(clamped_end));
        }

        std::vector<float> resample_linear_to_size(const std::vector<float>& input, const std::size_t target_size) {
            if (target_size == 0) { return {}; }
            if (input.empty()) { return std::vector<float>(target_size, 0.0f); }
            if (input.size() == target_size) { return input; }
            if (target_size == 1) { return {input.front()}; }

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

        std::vector<float> extract_with_offset(const std::vector<float>& input, const std::size_t offset_samples, const std::size_t target_samples) {
            if (target_samples == 0) { return {}; }
            std::vector out(target_samples, 0.0f);
            if (offset_samples >= input.size()) { return out; }
            const auto n = std::min<std::size_t>(target_samples, input.size() - offset_samples);
            std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(offset_samples), static_cast<std::ptrdiff_t>(n), out.begin());
            return out;
        }

        void apply_gain_db(std::vector<float>& audio, const double gain_db) {
            if (std::fabs(gain_db) <= 1.0e-9) { return; }
            const auto gain = static_cast<float>(std::pow(10.0, gain_db / 20.0));
            for (auto& s : audio) { s *= gain; }
        }

        void apply_edge_fade(std::vector<float>& audio, const int sample_rate, const double fade_seconds) {
            if (sample_rate <= 0 || audio.size() < 2 || fade_seconds <= 0.0) { return; }
            const auto fade_samples = std::min<std::size_t>(audio.size() / 2,std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(fade_seconds * static_cast<double>(sample_rate)))));
            if (fade_samples == 0) { return; }

            for (std::size_t i = 0; i < fade_samples; ++i) {
                const auto gain = static_cast<float>(static_cast<double>(i + 1) / static_cast<double>(fade_samples));
                audio[i] *= gain;
                audio[audio.size() - 1 - i] *= gain;
            }
        }

        std::vector<float> mono_to_interleaved_channels(const std::vector<float>& mono, const int channels) {
            if (channels <= 0) { throw std::invalid_argument("channels must be positive"); }
            if (channels == 1) { return mono; }
            std::vector interleaved(mono.size() * static_cast<std::size_t>(channels), 0.0f);
            for (std::size_t i = 0; i < mono.size(); ++i) {
                const std::size_t base = i * static_cast<std::size_t>(channels);
                for (int ch = 0; ch < channels; ++ch) { interleaved[base + static_cast<std::size_t>(ch)] = mono[i]; }
            }
            return interleaved;
        }

        std::string stem_filename(const std::int64_t track_id, const std::string& track_name) {
            std::string safe = track_name;
            for (auto& c : safe) { if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') { c = '_'; } }
            if (safe.empty()) { safe = "track"; }
            return safe + "_" + std::to_string(track_id) + ".wav";
        }

        void reassign_blob_ids_preserving_links(std::vector<core::NoteBlob>& blobs, const std::int64_t starting_id) {
            std::unordered_map<std::int64_t, std::int64_t> id_remap{};
            id_remap.reserve(blobs.size());

            std::int64_t next_id = starting_id;
            for (auto& blob : blobs) {
                id_remap.emplace(blob.id, next_id);
                blob.id = next_id++;
            }

            auto remap_link = [&](const std::optional<std::int64_t>& link) -> std::optional<std::int64_t> {
                if (!link.has_value()) { return std::nullopt; }
                const auto it = id_remap.find(link.value());
                if (it == id_remap.end()) { return std::nullopt; }
                return it->second;
            };

            for (auto& blob : blobs) {
                blob.link_prev = remap_link(blob.link_prev);
                blob.link_next = remap_link(blob.link_next);
            }
        }
    } // namespace

    Session::Session(std::shared_ptr<capabilities::IPitchExtractor> pitch_extractor, std::shared_ptr<capabilities::IVocoder> vocoder, capabilities::SegmenterConfig segmenter_config, render::RenderGroupingConfig render_grouping):
    chain_(std::move(pitch_extractor), std::move(vocoder), segmenter_config), planner_(render_grouping) {}

    std::int64_t Session::create_track(const std::string& name) {
        const std::int64_t id = next_track_id_++;
        TrackState track{};
        track.id = id;
        track.name = name.empty() ? ("Track " + std::to_string(id)) : name;
        tracks_.push_back(std::move(track));
        return id;
    }

    void Session::remove_track(const std::int64_t track_id) {
        if (const auto it = std::ranges::find_if(tracks_, [&](const TrackState& track) { return track.id == track_id; }); it != tracks_.end()) { tracks_.erase(it); }
    }

    void Session::clear() {
        tracks_.clear();
        next_track_id_ = 1;
    }

    // 覆盖式导入
    void Session::import_audio_to_track(const std::int64_t track_id, const std::vector<float>& interleaved_samples, const int sample_rate, const int channels) {
        auto* track = find_track(track_id);
        if (!track) { throw std::invalid_argument("track_id does not exist"); }

        const auto session_audio = normalize_to_session_audio(interleaved_samples, sample_rate, channels);
        track->duration_seconds = session_audio.empty() ? 0.0 : static_cast<double>(session_audio.size()) / static_cast<double>(kSessionSampleRate);

        auto analysis = chain_.analyze_and_segment(session_audio, kSessionSampleRate);
        auto& blobs = analysis.blobs;
        track->blobs.clear(); // 清除原先轨道音符块
        track->blobs.reserve(blobs.size());
        reassign_blob_ids_preserving_links(blobs, 1);
        for (auto& blob : blobs) {
            if (blob.edit_revision == 0) { blob.edit_revision = 1; }

            core::TimeRange source_range{
                .start_seconds = blob.original_start_seconds,
                .end_seconds = blob.original_end_seconds(),
            };
            if (!source_range.is_valid() || source_range.length() <= 0.0) { source_range = blob.time; }
            blob.source_audio_44k = slice_audio_range(session_audio, kSessionSampleRate, source_range);
            chain_.prepare_blob(blob, kSessionSampleRate);
            track->blobs.push_back(std::move(blob));
        }

        track->render_units = planner_.plan(track->blobs);
        track->rendered_blob_cache.clear(); // 清理旧缓存
        refresh_derived_dirty(*track);
    }

    // 增量式插入。插到任意秒
    void Session::insert_audio_to_track(const std::int64_t track_id, const std::vector<float>& interleaved_samples, const int sample_rate, const int channels, const double insert_time_seconds) {
        auto* track = find_track(track_id);
        if (!track) { throw std::invalid_argument("track_id does not exist"); }

        const auto session_audio = normalize_to_session_audio(interleaved_samples, sample_rate, channels);
        auto analysis = chain_.analyze_and_segment(session_audio, kSessionSampleRate);
        auto& blobs = analysis.blobs;
        if (blobs.empty()) { return; }

        std::int64_t next_blob_id = 1;
        for (const auto& existing : track->blobs) { next_blob_id = std::max(next_blob_id, existing.id + 1); }

        const double t0 = std::max(0.0, insert_time_seconds);
        reassign_blob_ids_preserving_links(blobs, next_blob_id);
        std::vector<core::NoteBlob> inserted{};
        inserted.reserve(blobs.size());
        for (auto& blob : blobs) {
            core::TimeRange local_source{
                .start_seconds = blob.original_start_seconds,
                .end_seconds = blob.original_end_seconds(),
            };
            if (!local_source.is_valid() || local_source.length() <= 0.0) { local_source = blob.time; }

            blob.time.start_seconds += t0;
            blob.time.end_seconds += t0;
            blob.original_start_seconds += t0;
            for (auto& p : blob.original_pitch_curve) p.seconds += t0;
            blob.detached = false;

            blob.source_audio_44k = slice_audio_range(session_audio, kSessionSampleRate, local_source);
            chain_.prepare_blob(blob, kSessionSampleRate);
            inserted.push_back(std::move(blob));
        }

        track->blobs.insert(track->blobs.end(), inserted.begin(), inserted.end());
        std::sort(track->blobs.begin(), track->blobs.end(), [](const core::NoteBlob& a, const core::NoteBlob& b) {
            if (a.time.start_seconds != b.time.start_seconds) { return a.time.start_seconds < b.time.start_seconds; }
            return a.id < b.id;
        });
        track->duration_seconds = std::max(
            track->duration_seconds,
            t0 + (static_cast<double>(session_audio.size()) / static_cast<double>(kSessionSampleRate)));
        for (const auto& blob : inserted) { track->rendered_blob_cache.erase(blob.id); }
        track->render_units = planner_.plan(track->blobs);
        refresh_derived_dirty(*track);
    }

    std::int64_t Session::import_audio_as_new_track(const std::vector<float>& interleaved_samples, const int sample_rate, const int channels, const std::string& name) {
        const auto track_id = create_track(name);
        import_audio_to_track(track_id, interleaved_samples, sample_rate, channels);
        return track_id;
    }

    void Session::set_track_mute(const std::int64_t track_id, const bool mute) {
        auto* track = find_track(track_id);
        if (!track) { throw std::invalid_argument("track_id does not exist"); }
        track->mix.mute = mute;
    }

    void Session::set_track_solo(const std::int64_t track_id, const bool solo) {
        auto* track = find_track(track_id);
        if (!track) { throw std::invalid_argument("track_id does not exist"); }
        track->mix.solo = solo;
    }

    void Session::set_track_gain_db(const std::int64_t track_id, const double gain_db) {
        auto* track = find_track(track_id);
        if (!track) { throw std::invalid_argument("track_id does not exist"); }
        if (!std::isfinite(gain_db)) { throw std::invalid_argument("gain_db must be finite"); }
        track->mix.gain_db = gain_db;
    }

    void Session::shift_blob_pitch(const std::int64_t track_id, const std::int64_t blob_id, const double semitones) {
        auto* track = find_track(track_id);
        if (!track) { throw std::invalid_argument("track_id does not exist"); }
        auto* blob = find_blob(*track, blob_id);
        if (!blob) { return; }
        core::NoteBlobOps::shift_pitch(*blob, semitones);
        track->render_units = planner_.plan(track->blobs);
        refresh_derived_dirty(*track);
    }

    void Session::stretch_blob_time(const std::int64_t track_id, const std::int64_t blob_id, const double new_duration_seconds) {
        auto* track = find_track(track_id);
        if (!track) { throw std::invalid_argument("track_id does not exist"); }
        auto* blob = find_blob(*track, blob_id);
        if (!blob) { return; }
        core::NoteBlobOps::stretch_time(*blob, new_duration_seconds);
        track->render_units = planner_.plan(track->blobs);
        refresh_derived_dirty(*track);
    }

    bool Session::is_unit_dirty(const TrackState& track, const render::RenderUnit& unit) const {
        for (const auto note_id : unit.note_ids) {
            const auto* live = find_blob(track, note_id);
            if (!live) { return true; }

            const auto cache_it = track.rendered_blob_cache.find(live->id);
            if (cache_it == track.rendered_blob_cache.end()) { return true; }
            if (cache_it->second.source_revision != live->edit_revision) { return true; }
        }
        return false;
    }

    void Session::refresh_derived_dirty(TrackState& track) const {
        track.dirty_timeline.reset(track.duration_seconds);
        for (const auto& unit : track.render_units) { if (is_unit_dirty(track, unit)) { track.dirty_timeline.mark_dirty(unit.span); } }
    }

    std::vector<TrackRenderPlan> Session::plan_render_from(const double playhead_seconds, const std::size_t budget_units_per_track) const {
        std::vector<TrackRenderPlan> plans{};
        plans.reserve(tracks_.size());
        for (const auto& track : tracks_) {
            auto units = scheduler_.plan_from_playhead(playhead_seconds, track.render_units, track.dirty_timeline, budget_units_per_track);
            if (!units.empty()) { plans.push_back(TrackRenderPlan{.track_id = track.id, .units = std::move(units)}); }
        }
        return plans;
    }

    void Session::render_units(const std::vector<TrackRenderPlan>& plans) {
        for (const auto& plan : plans) {
            auto* track = find_track(plan.track_id);
            if (!track) { continue; }

            for (const auto& unit : plan.units) {
                std::vector<const core::NoteBlob*> live_blobs{};
                live_blobs.reserve(unit.note_ids.size());
                for (const auto note_id : unit.note_ids) {
                    const auto* live_blob = find_blob(*track, note_id);
                    if (live_blob) { live_blobs.push_back(live_blob); }
                }
                if (live_blobs.empty()) { continue; }

                bool all_unedited = true;
                for (const auto* blob : live_blobs) {
                    if (!blob->is_unedited()) {
                        all_unedited = false;
                        break;
                    }
                }

                if (all_unedited) {
                    for (const auto* live_blob : live_blobs) {
                        const auto target_samples = static_cast<std::size_t>(
                            std::max(0.0, live_blob->duration()) * static_cast<double>(kSessionSampleRate));
                        auto rendered = live_blob->source_audio_44k;
                        if (target_samples > 0 && rendered.size() != target_samples) { rendered = resample_linear_to_size(rendered, target_samples); }
                        if (live_blob->is_unvoiced_only()) { apply_edge_fade(rendered, kSessionSampleRate, kUnvoicedBlobFadeSeconds); }
                        apply_gain_db(rendered, live_blob->loudness_gain_db);
                        track->rendered_blob_cache[live_blob->id] = RenderedBlobCache{
                            .audio = std::move(rendered),
                            .source_revision = live_blob->edit_revision,
                        };
                    }
                    continue;
                }

                std::vector<core::NoteBlob> group_blobs{};
                group_blobs.reserve(live_blobs.size());
                for (const auto* b : live_blobs) { group_blobs.push_back(*b); }

                const auto group_audio = chain_.resynthesize_group(group_blobs, kSessionSampleRate);
                for (const auto* live_blob : live_blobs) {
                    const auto target_samples = static_cast<std::size_t>(
                        std::max(0.0, live_blob->duration()) * static_cast<double>(kSessionSampleRate));
                    const auto offset_samples = static_cast<std::size_t>(
                        std::max(0.0, live_blob->time.start_seconds - unit.span.start_seconds) * static_cast<double>(kSessionSampleRate));
                    auto rendered = extract_with_offset(group_audio, offset_samples, target_samples);
                    apply_gain_db(rendered, live_blob->loudness_gain_db);
                    track->rendered_blob_cache[live_blob->id] = RenderedBlobCache{
                        .audio = std::move(rendered),
                        .source_revision = live_blob->edit_revision,
                    };
                }
            }
            refresh_derived_dirty(*track);
        }
    }

    void Session::render_all_dirty(const std::size_t budget_units_per_track) {
        if (budget_units_per_track == 0) { return; }

        for (auto& track : tracks_) {
            std::size_t stagnant_rounds = 0;
            while (!track.dirty_timeline.dirty_ranges().empty()) {
                const auto before = track.dirty_timeline.dirty_ranges().size();
                const auto playhead = track.dirty_timeline.dirty_ranges().front().start_seconds;
                auto units = scheduler_.plan_from_playhead(playhead, track.render_units, track.dirty_timeline, budget_units_per_track);
                if (units.empty()) { break; }

                render_units({TrackRenderPlan{.track_id = track.id, .units = std::move(units)}});

                const auto after = track.dirty_timeline.dirty_ranges().size();
                if (after >= before) {
                    ++stagnant_rounds;
                    if (stagnant_rounds >= 4) { throw std::runtime_error("render_all_dirty stalled: dirty ranges not shrinking"); }
                }
                else { stagnant_rounds = 0; }
            }
        }
    }

    bool Session::is_track_active_in_mix(const TrackState& track) const {
        const bool has_solo = std::any_of(tracks_.begin(), tracks_.end(), [](const TrackState& t) { return t.mix.solo; });
        return has_solo ? track.mix.solo : !track.mix.mute;
    }

    std::vector<float> Session::build_track_audio(const std::int64_t track_id, const bool apply_track_gain) const {
        const auto* track = find_track(track_id);
        if (!track) { throw std::invalid_argument("track_id does not exist"); }

        std::size_t output_size = 0;
        for (const auto& blob : track->blobs) {
            const auto it = track->rendered_blob_cache.find(blob.id);
            if (it == track->rendered_blob_cache.end()) { continue; }
            const auto start = static_cast<std::size_t>(std::max(0.0, blob.time.start_seconds) * static_cast<double>(kSessionSampleRate));
            output_size = std::max(output_size, start + it->second.audio.size());
        }

        std::vector<float> mixed(output_size, 0.0f);
        for (const auto& blob : track->blobs) {
            const auto it = track->rendered_blob_cache.find(blob.id);
            if (it == track->rendered_blob_cache.end()) { continue; }
            const auto start = static_cast<std::size_t>(std::max(0.0, blob.time.start_seconds) * static_cast<double>(kSessionSampleRate));
            const auto& clip = it->second.audio;
            for (std::size_t i = 0; i < clip.size(); ++i) {
                const auto out_index = start + i;
                if (out_index >= mixed.size()) { break; }
                mixed[out_index] += clip[i];
            }
        }

        if (apply_track_gain) { apply_gain_db(mixed, track->mix.gain_db); }
        for (auto& s : mixed) { s = std::clamp(s, -1.0f, 1.0f); }
        return mixed;
    }

    std::vector<float> Session::build_mixdown() const {
        std::size_t output_size = 0;
        std::vector<std::vector<float>> active_tracks{};

        for (const auto& track : tracks_) {
            if (!is_track_active_in_mix(track)) { continue; }
            active_tracks.push_back(build_track_audio(track.id, true));
            output_size = std::max(output_size, active_tracks.back().size());
        }

        std::vector<float> mix(output_size, 0.0f);
        for (const auto& audio : active_tracks) { for (std::size_t i = 0; i < audio.size(); ++i) { mix[i] += audio[i]; } }
        for (auto& s : mix) { s = std::clamp(s, -1.0f, 1.0f); }
        return mix;
    }

    void Session::export_audio(const ExportRequest& request) const {
        if (request.format.sample_rate <= 0) { throw std::invalid_argument("export sample_rate must be positive"); }
        if (request.format.channels <= 0) { throw std::invalid_argument("export channels must be positive"); }

        auto convert_for_export = [&](const std::vector<float>& mono_44k) {
            const auto mono_target = request.format.sample_rate == kSessionSampleRate
                                         ? mono_44k
                                         : resample_linear_rate(mono_44k, kSessionSampleRate, request.format.sample_rate);
            return mono_to_interleaved_channels(mono_target, request.format.channels);
        };

        io::WavWriteSpec spec{};
        spec.sample_rate = request.format.sample_rate;
        spec.channels = request.format.channels;
        spec.bits_per_sample = request.format.bits_per_sample;
        spec.ieee_float = request.format.ieee_float;

        if (request.mode == ExportMode::Mixdown) {
            if (request.output_path.empty()) { throw std::invalid_argument("mixdown output_path is empty"); }
            const auto mix = build_mixdown();
            const auto interleaved = convert_for_export(mix);
            io::write_wav(request.output_path, interleaved, spec);
            return;
        }

        if (request.stems_directory.empty()) { throw std::invalid_argument("stems_directory is empty"); }
        std::filesystem::create_directories(request.stems_directory);
        for (const auto& track : tracks_) {
            if (request.stems_respect_mute_solo && !is_track_active_in_mix(track)) { continue; }
            const auto mono = build_track_audio(track.id, true);
            const auto interleaved = convert_for_export(mono);
            const auto out_path = (std::filesystem::path(request.stems_directory) / stem_filename(track.id, track.name)).string();
            io::write_wav(out_path, interleaved, spec);
        }
    }

    project::ProjectState Session::capture_project_state() const {
        project::ProjectState out{};
        out.session_sample_rate = kSessionSampleRate;
        out.duration_seconds = duration_seconds();
        out.tracks.reserve(tracks_.size());

        for (const auto& track : tracks_) {
            project::TrackProjectState t{};
            t.id = track.id;
            t.name = track.name;
            t.mute = track.mix.mute;
            t.solo = track.mix.solo;
            t.gain_db = track.mix.gain_db;
            t.duration_seconds = track.duration_seconds;
            t.blobs = track.blobs;
            out.tracks.push_back(std::move(t));
        }
        return out;
    }

    void Session::restore_project_state(const project::ProjectState& state) {
        if (state.session_sample_rate != kSessionSampleRate) { throw std::invalid_argument("project state session_sample_rate mismatch"); }

        tracks_.clear();
        next_track_id_ = 1;
        tracks_.reserve(state.tracks.size());
        for (const auto& saved : state.tracks) {
            TrackState track{};
            track.id = saved.id > 0 ? saved.id : next_track_id_++;
            next_track_id_ = std::max(next_track_id_, track.id + 1);
            track.name = saved.name.empty() ? ("Track " + std::to_string(track.id)) : saved.name;
            track.mix = TrackMixState{.mute = saved.mute, .solo = saved.solo, .gain_db = saved.gain_db};
            track.blobs = saved.blobs;
            for (auto& blob : track.blobs) {
                if (blob.edit_revision == 0) { blob.edit_revision = 1; }
                blob.cached_source_mel_bins = 0;
                blob.cached_source_mel_frames = 0;
                blob.cached_source_mel_log.clear();
            }
            track.duration_seconds = saved.duration_seconds;
            track.render_units = planner_.plan(track.blobs);
            track.rendered_blob_cache.clear();
            refresh_derived_dirty(track);
            tracks_.push_back(std::move(track));
        }
    }

    std::vector<TrackInfo> Session::tracks() const {
        std::vector<TrackInfo> out{};
        out.reserve(tracks_.size());
        for (const auto& track : tracks_) {
            out.push_back(TrackInfo{
                .id = track.id,
                .name = track.name,
                .mix = track.mix,
                .duration_seconds = track.duration_seconds,
                .blob_count = track.blobs.size(),
                .has_dirty = !track.dirty_timeline.dirty_ranges().empty(),
            });
        }
        return out;
    }

    const std::vector<core::NoteBlob>& Session::track_blobs(const std::int64_t track_id) const {
        const auto* track = find_track(track_id);
        if (!track) { throw std::invalid_argument("track_id does not exist"); }
        return track->blobs;
    }

    const render::DirtyTimeline& Session::track_dirty_timeline(const std::int64_t track_id) const {
        const auto* track = find_track(track_id);
        if (!track) { throw std::invalid_argument("track_id does not exist"); }
        return track->dirty_timeline;
    }

    double Session::duration_seconds() const {
        double d = 0.0;
        for (const auto& track : tracks_) { d = std::max(d, track.duration_seconds); }
        return d;
    }

    Session::TrackState* Session::find_track(const std::int64_t track_id) {
        for (auto& track : tracks_) { if (track.id == track_id) { return &track; } }
        return nullptr;
    }

    const Session::TrackState* Session::find_track(const std::int64_t track_id) const {
        for (const auto& track : tracks_) { if (track.id == track_id) { return &track; } }
        return nullptr;
    }

    core::NoteBlob* Session::find_blob(TrackState& track, const std::int64_t blob_id) {
        for (auto& blob : track.blobs) { if (blob.id == blob_id) { return &blob; } }
        return nullptr;
    }

    const core::NoteBlob* Session::find_blob(const TrackState& track, const std::int64_t blob_id) const {
        for (const auto& blob : track.blobs) { if (blob.id == blob_id) { return &blob; } }
        return nullptr;
    }
} // namespace melodick::app
