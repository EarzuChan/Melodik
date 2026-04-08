#include "melodick/project/project_state.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <winsqlite/winsqlite3.h>

namespace melodick::project {

namespace {

constexpr int kSchemaVersion = 4;

[[noreturn]] void throw_sqlite_error(sqlite3* db, const std::string& where) {
    const char* msg = db ? sqlite3_errmsg(db) : "sqlite unavailable";
    throw std::runtime_error(where + ": " + msg);
}

void check_sqlite(int rc, sqlite3* db, const std::string& where) {
    if (rc == SQLITE_OK || rc == SQLITE_DONE || rc == SQLITE_ROW) {
        return;
    }
    throw_sqlite_error(db, where);
}

class Db {
public:
    explicit Db(const std::string& path, const int flags) {
        sqlite3* raw = nullptr;
        const int rc = sqlite3_open_v2(path.c_str(), &raw, flags, nullptr);
        if (rc != SQLITE_OK || raw == nullptr) {
            if (raw != nullptr) {
                const std::string msg = sqlite3_errmsg(raw);
                sqlite3_close(raw);
                throw std::runtime_error("sqlite open failed: " + msg);
            }
            throw std::runtime_error("sqlite open failed");
        }
        db_ = raw;
    }

    ~Db() {
        if (db_ != nullptr) {
            sqlite3_close(db_);
        }
    }

    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    sqlite3* get() const { return db_; }

private:
    sqlite3* db_ {nullptr};
};

class Stmt {
public:
    Stmt(sqlite3* db, const std::string& sql)
        : db_(db) {
        check_sqlite(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr), db, "sqlite prepare");
    }

    ~Stmt() {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
    }

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    sqlite3_stmt* get() const { return stmt_; }

    void reset() const {
        check_sqlite(sqlite3_reset(stmt_), db_, "sqlite reset");
        check_sqlite(sqlite3_clear_bindings(stmt_), db_, "sqlite clear_bindings");
    }

private:
    sqlite3* db_ {nullptr};
    sqlite3_stmt* stmt_ {nullptr};
};

void exec_sql(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "sqlite exec failed";
        if (err != nullptr) {
            sqlite3_free(err);
        }
        throw std::runtime_error(msg);
    }
}

template <typename T>
void append_scalar(std::vector<std::uint8_t>& out, const T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* ptr = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), ptr, ptr + sizeof(T));
}

template <typename T>
T read_scalar(const std::vector<std::uint8_t>& src, std::size_t& off) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (off + sizeof(T) > src.size()) {
        throw std::runtime_error("project blob decode overflow");
    }
    T out {};
    std::memcpy(&out, src.data() + off, sizeof(T));
    off += sizeof(T);
    return out;
}

std::vector<std::uint8_t> pack_float_vector(const std::vector<float>& values) {
    std::vector<std::uint8_t> out {};
    out.reserve(sizeof(std::uint32_t) + values.size() * sizeof(float));
    append_scalar<std::uint32_t>(out, static_cast<std::uint32_t>(values.size()));
    for (const auto v : values) {
        append_scalar<float>(out, v);
    }
    return out;
}

std::vector<float> unpack_float_vector(const std::vector<std::uint8_t>& data) {
    std::size_t off = 0;
    const auto count = static_cast<std::size_t>(read_scalar<std::uint32_t>(data, off));
    std::vector<float> out {};
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(read_scalar<float>(data, off));
    }
    if (off != data.size()) {
        throw std::runtime_error("project blob decode trailing data");
    }
    return out;
}

std::vector<std::uint8_t> pack_pitch_slice(const core::PitchSlice& points) {
    std::vector<std::uint8_t> out {};
    out.reserve(sizeof(std::uint32_t) + points.size() * (sizeof(double) * 2 + sizeof(std::uint8_t) + sizeof(float)));
    append_scalar<std::uint32_t>(out, static_cast<std::uint32_t>(points.size()));
    for (const auto& p : points) {
        append_scalar<double>(out, p.seconds);
        append_scalar<double>(out, p.midi);
        append_scalar<std::uint8_t>(out, p.voiced ? 1 : 0);
        append_scalar<float>(out, p.confidence);
    }
    return out;
}

core::PitchSlice unpack_pitch_slice(const std::vector<std::uint8_t>& data) {
    std::size_t off = 0;
    const auto count = static_cast<std::size_t>(read_scalar<std::uint32_t>(data, off));
    core::PitchSlice out {};
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        core::PitchPoint p {};
        p.seconds = read_scalar<double>(data, off);
        p.midi = read_scalar<double>(data, off);
        p.voiced = read_scalar<std::uint8_t>(data, off) != 0;
        p.confidence = read_scalar<float>(data, off);
        out.push_back(p);
    }
    if (off != data.size()) {
        throw std::runtime_error("project pitch decode trailing data");
    }
    return out;
}

std::vector<std::uint8_t> pack_line_patches(const std::vector<core::LinePatch>& lines) {
    std::vector<std::uint8_t> out {};
    out.reserve(sizeof(std::uint32_t) + lines.size() * (sizeof(std::int32_t) + sizeof(double) * 6));
    append_scalar<std::uint32_t>(out, static_cast<std::uint32_t>(lines.size()));
    for (const auto& line : lines) {
        append_scalar<std::int32_t>(out, static_cast<std::int32_t>(line.type));
        append_scalar<double>(out, line.start_u);
        append_scalar<double>(out, line.end_u);
        append_scalar<double>(out, line.start_delta_midi);
        append_scalar<double>(out, line.end_delta_midi);
        append_scalar<double>(out, line.vibrato_depth_midi);
        append_scalar<double>(out, line.vibrato_cycles);
    }
    return out;
}

std::vector<core::LinePatch> unpack_line_patches(const std::vector<std::uint8_t>& data) {
    std::size_t off = 0;
    const auto count = static_cast<std::size_t>(read_scalar<std::uint32_t>(data, off));
    std::vector<core::LinePatch> out {};
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        core::LinePatch line {};
        line.type = static_cast<core::LinePatchType>(read_scalar<std::int32_t>(data, off));
        line.start_u = read_scalar<double>(data, off);
        line.end_u = read_scalar<double>(data, off);
        line.start_delta_midi = read_scalar<double>(data, off);
        line.end_delta_midi = read_scalar<double>(data, off);
        line.vibrato_depth_midi = read_scalar<double>(data, off);
        line.vibrato_cycles = read_scalar<double>(data, off);
        out.push_back(line);
    }
    if (off != data.size()) {
        throw std::runtime_error("project line patch decode trailing data");
    }
    return out;
}

std::vector<std::uint8_t> read_blob_column(sqlite3_stmt* stmt, const int index) {
    const auto size = sqlite3_column_bytes(stmt, index);
    const auto* ptr = static_cast<const std::uint8_t*>(sqlite3_column_blob(stmt, index));
    if (size <= 0) {
        return {};
    }
    if (ptr == nullptr) {
        throw std::runtime_error("sqlite blob column is null with nonzero size");
    }
    return std::vector<std::uint8_t>(ptr, ptr + size);
}

void bind_blob(sqlite3* db, sqlite3_stmt* stmt, const int index, const std::vector<std::uint8_t>& bytes) {
    const void* data = bytes.empty() ? nullptr : bytes.data();
    check_sqlite(
        sqlite3_bind_blob(stmt, index, data, static_cast<int>(bytes.size()), SQLITE_TRANSIENT),
        db,
        "sqlite bind blob");
}

void bind_text(sqlite3* db, sqlite3_stmt* stmt, const int index, const std::string& value) {
    check_sqlite(sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT), db, "sqlite bind text");
}

std::string require_meta_value(sqlite3* db, const std::string& key) {
    Stmt stmt {db, "SELECT value FROM meta WHERE key=?1;"};
    bind_text(db, stmt.get(), 1, key);
    const int rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_ROW) {
        throw std::runtime_error("project missing meta key: " + key);
    }
    const auto* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
    return txt ? std::string(txt) : std::string {};
}

void create_schema(sqlite3* db) {
    exec_sql(db, "PRAGMA foreign_keys=ON;");
    exec_sql(db, "CREATE TABLE IF NOT EXISTS meta("
                 "key TEXT PRIMARY KEY,"
                 "value TEXT NOT NULL"
                 ");");
    exec_sql(db, "CREATE TABLE IF NOT EXISTS tracks("
                 "track_index INTEGER NOT NULL,"
                 "id INTEGER NOT NULL PRIMARY KEY,"
                 "name TEXT NOT NULL,"
                 "mute INTEGER NOT NULL,"
                 "solo INTEGER NOT NULL,"
                 "gain_db REAL NOT NULL,"
                 "duration_seconds REAL NOT NULL"
                 ");");
    exec_sql(db, "CREATE TABLE IF NOT EXISTS blobs("
                 "track_id INTEGER NOT NULL,"
                 "blob_index INTEGER NOT NULL,"
                 "id INTEGER NOT NULL,"
                 "start_seconds REAL NOT NULL,"
                 "end_seconds REAL NOT NULL,"
                 "original_start_seconds REAL NOT NULL,"
                 "original_duration_seconds REAL NOT NULL,"
                 "global_transpose_semitones REAL NOT NULL,"
                 "time_ratio REAL NOT NULL,"
                 "loudness_gain_db REAL NOT NULL,"
                 "detached INTEGER NOT NULL,"
                 "edit_revision INTEGER NOT NULL,"
                 "link_prev INTEGER,"
                 "link_next INTEGER,"
                 "source_mel_bins INTEGER NOT NULL,"
                 "source_mel_frames INTEGER NOT NULL,"
                 "original_pitch_blob BLOB NOT NULL,"
                 "source_audio_blob BLOB NOT NULL,"
                 "source_mel_blob BLOB NOT NULL,"
                 "handdraw_patch_blob BLOB NOT NULL,"
                 "line_patches_blob BLOB NOT NULL,"
                 "PRIMARY KEY(track_id, id),"
                 "FOREIGN KEY(track_id) REFERENCES tracks(id) ON DELETE CASCADE"
                 ");");
}

void clear_all_rows(sqlite3* db) {
    exec_sql(db, "DELETE FROM blobs;");
    exec_sql(db, "DELETE FROM tracks;");
    exec_sql(db, "DELETE FROM meta;");
}

} // namespace

void save_project_state(const std::string& path, const ProjectState& state) {
    Db db {path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE};
    create_schema(db.get());

    exec_sql(db.get(), "BEGIN IMMEDIATE TRANSACTION;");
    try {
        clear_all_rows(db.get());

        Stmt insert_meta {db.get(), "INSERT INTO meta(key, value) VALUES(?1, ?2);"};
        auto write_meta = [&](const std::string& key, const std::string& value) {
            insert_meta.reset();
            bind_text(db.get(), insert_meta.get(), 1, key);
            bind_text(db.get(), insert_meta.get(), 2, value);
            check_sqlite(sqlite3_step(insert_meta.get()), db.get(), "sqlite insert meta");
        };
        write_meta("schema_version", std::to_string(kSchemaVersion));
        write_meta("session_sample_rate", std::to_string(state.session_sample_rate));
        write_meta("duration_seconds", std::to_string(state.duration_seconds));

        Stmt insert_track {
            db.get(),
            "INSERT INTO tracks(track_index, id, name, mute, solo, gain_db, duration_seconds)"
            " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7);"};
        Stmt insert_blob {
            db.get(),
            "INSERT INTO blobs("
            "track_id, blob_index, id, start_seconds, end_seconds, original_start_seconds, original_duration_seconds,"
            "global_transpose_semitones, time_ratio, loudness_gain_db, detached, edit_revision, link_prev, link_next,"
            "source_mel_bins, source_mel_frames, original_pitch_blob, source_audio_blob, source_mel_blob, handdraw_patch_blob, line_patches_blob"
            ") VALUES("
            "?1, ?2, ?3, ?4, ?5, ?6, ?7,"
            "?8, ?9, ?10, ?11, ?12, ?13, ?14,"
            "?15, ?16, ?17, ?18, ?19, ?20, ?21"
            ");"};

        for (std::size_t ti = 0; ti < state.tracks.size(); ++ti) {
            const auto& track = state.tracks[ti];
            insert_track.reset();
            check_sqlite(sqlite3_bind_int64(insert_track.get(), 1, static_cast<sqlite3_int64>(ti)), db.get(), "sqlite bind track_index");
            check_sqlite(sqlite3_bind_int64(insert_track.get(), 2, static_cast<sqlite3_int64>(track.id)), db.get(), "sqlite bind track id");
            bind_text(db.get(), insert_track.get(), 3, track.name);
            check_sqlite(sqlite3_bind_int(insert_track.get(), 4, track.mute ? 1 : 0), db.get(), "sqlite bind track mute");
            check_sqlite(sqlite3_bind_int(insert_track.get(), 5, track.solo ? 1 : 0), db.get(), "sqlite bind track solo");
            check_sqlite(sqlite3_bind_double(insert_track.get(), 6, track.gain_db), db.get(), "sqlite bind track gain");
            check_sqlite(sqlite3_bind_double(insert_track.get(), 7, track.duration_seconds), db.get(), "sqlite bind track duration");
            check_sqlite(sqlite3_step(insert_track.get()), db.get(), "sqlite insert track");

            for (std::size_t bi = 0; bi < track.blobs.size(); ++bi) {
                const auto& blob = track.blobs[bi];
                insert_blob.reset();
                check_sqlite(sqlite3_bind_int64(insert_blob.get(), 1, static_cast<sqlite3_int64>(track.id)), db.get(), "sqlite bind blob track_id");
                check_sqlite(sqlite3_bind_int64(insert_blob.get(), 2, static_cast<sqlite3_int64>(bi)), db.get(), "sqlite bind blob index");
                check_sqlite(sqlite3_bind_int64(insert_blob.get(), 3, static_cast<sqlite3_int64>(blob.id)), db.get(), "sqlite bind blob id");
                check_sqlite(sqlite3_bind_double(insert_blob.get(), 4, blob.time.start_seconds), db.get(), "sqlite bind blob start");
                check_sqlite(sqlite3_bind_double(insert_blob.get(), 5, blob.time.end_seconds), db.get(), "sqlite bind blob end");
                check_sqlite(sqlite3_bind_double(insert_blob.get(), 6, blob.original_start_seconds), db.get(), "sqlite bind blob original start");
                check_sqlite(sqlite3_bind_double(insert_blob.get(), 7, blob.original_duration_seconds), db.get(), "sqlite bind blob original duration");
                check_sqlite(sqlite3_bind_double(insert_blob.get(), 8, blob.global_transpose_semitones), db.get(), "sqlite bind blob transpose");
                check_sqlite(sqlite3_bind_double(insert_blob.get(), 9, blob.time_ratio), db.get(), "sqlite bind blob ratio");
                check_sqlite(sqlite3_bind_double(insert_blob.get(), 10, blob.loudness_gain_db), db.get(), "sqlite bind blob loudness");
                check_sqlite(sqlite3_bind_int(insert_blob.get(), 11, blob.detached ? 1 : 0), db.get(), "sqlite bind blob detached");
                check_sqlite(sqlite3_bind_int64(insert_blob.get(), 12, static_cast<sqlite3_int64>(blob.edit_revision)), db.get(), "sqlite bind blob revision");
                if (blob.link_prev.has_value()) {
                    check_sqlite(sqlite3_bind_int64(insert_blob.get(), 13, static_cast<sqlite3_int64>(blob.link_prev.value())), db.get(), "sqlite bind blob link_prev");
                } else {
                    check_sqlite(sqlite3_bind_null(insert_blob.get(), 13), db.get(), "sqlite bind blob link_prev null");
                }
                if (blob.link_next.has_value()) {
                    check_sqlite(sqlite3_bind_int64(insert_blob.get(), 14, static_cast<sqlite3_int64>(blob.link_next.value())), db.get(), "sqlite bind blob link_next");
                } else {
                    check_sqlite(sqlite3_bind_null(insert_blob.get(), 14), db.get(), "sqlite bind blob link_next null");
                }
                check_sqlite(sqlite3_bind_int(insert_blob.get(), 15, blob.source_mel_bins), db.get(), "sqlite bind mel bins");
                check_sqlite(sqlite3_bind_int(insert_blob.get(), 16, blob.source_mel_frames), db.get(), "sqlite bind mel frames");

                const auto original_pitch_blob = pack_pitch_slice(blob.original_pitch_curve);
                const auto source_audio_blob = pack_float_vector(blob.source_audio_44k);
                const auto source_mel_blob = pack_float_vector(blob.source_mel_log);
                const auto handdraw_blob = pack_float_vector(blob.handdraw_patch_midi);
                const auto lines_blob = pack_line_patches(blob.line_patches);
                bind_blob(db.get(), insert_blob.get(), 17, original_pitch_blob);
                bind_blob(db.get(), insert_blob.get(), 18, source_audio_blob);
                bind_blob(db.get(), insert_blob.get(), 19, source_mel_blob);
                bind_blob(db.get(), insert_blob.get(), 20, handdraw_blob);
                bind_blob(db.get(), insert_blob.get(), 21, lines_blob);
                check_sqlite(sqlite3_step(insert_blob.get()), db.get(), "sqlite insert blob");
            }
        }

        exec_sql(db.get(), "COMMIT;");
    } catch (...) {
        exec_sql(db.get(), "ROLLBACK;");
        throw;
    }
}

ProjectState load_project_state(const std::string& path) {
    Db db {path, SQLITE_OPEN_READONLY};

    const auto schema = std::stoi(require_meta_value(db.get(), "schema_version"));
    if (schema != kSchemaVersion) {
        throw std::runtime_error("unsupported project schema version");
    }

    ProjectState out {};
    out.session_sample_rate = std::stoi(require_meta_value(db.get(), "session_sample_rate"));
    out.duration_seconds = std::stod(require_meta_value(db.get(), "duration_seconds"));

    Stmt select_tracks {
        db.get(),
        "SELECT id, name, mute, solo, gain_db, duration_seconds "
        "FROM tracks ORDER BY track_index ASC;"};
    Stmt select_blobs {
        db.get(),
        "SELECT id, start_seconds, end_seconds, original_start_seconds, original_duration_seconds,"
        "global_transpose_semitones, time_ratio, loudness_gain_db, detached, edit_revision, link_prev, link_next,"
        "source_mel_bins, source_mel_frames, original_pitch_blob, source_audio_blob, source_mel_blob, handdraw_patch_blob, line_patches_blob "
        "FROM blobs WHERE track_id=?1 ORDER BY blob_index ASC;"};

    while (sqlite3_step(select_tracks.get()) == SQLITE_ROW) {
        TrackProjectState track {};
        track.id = static_cast<std::int64_t>(sqlite3_column_int64(select_tracks.get(), 0));
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(select_tracks.get(), 1));
        track.name = name ? std::string(name) : std::string {};
        track.mute = sqlite3_column_int(select_tracks.get(), 2) != 0;
        track.solo = sqlite3_column_int(select_tracks.get(), 3) != 0;
        track.gain_db = sqlite3_column_double(select_tracks.get(), 4);
        track.duration_seconds = sqlite3_column_double(select_tracks.get(), 5);

        select_blobs.reset();
        check_sqlite(sqlite3_bind_int64(select_blobs.get(), 1, static_cast<sqlite3_int64>(track.id)), db.get(), "sqlite bind select blobs track id");
        while (sqlite3_step(select_blobs.get()) == SQLITE_ROW) {
            core::NoteBlob blob {};
            blob.id = static_cast<std::int64_t>(sqlite3_column_int64(select_blobs.get(), 0));
            blob.time.start_seconds = sqlite3_column_double(select_blobs.get(), 1);
            blob.time.end_seconds = sqlite3_column_double(select_blobs.get(), 2);
            blob.original_start_seconds = sqlite3_column_double(select_blobs.get(), 3);
            blob.original_duration_seconds = sqlite3_column_double(select_blobs.get(), 4);
            blob.global_transpose_semitones = sqlite3_column_double(select_blobs.get(), 5);
            blob.time_ratio = sqlite3_column_double(select_blobs.get(), 6);
            blob.loudness_gain_db = sqlite3_column_double(select_blobs.get(), 7);
            blob.detached = sqlite3_column_int(select_blobs.get(), 8) != 0;
            blob.edit_revision = static_cast<std::uint64_t>(sqlite3_column_int64(select_blobs.get(), 9));
            if (sqlite3_column_type(select_blobs.get(), 10) != SQLITE_NULL) {
                blob.link_prev = static_cast<std::int64_t>(sqlite3_column_int64(select_blobs.get(), 10));
            }
            if (sqlite3_column_type(select_blobs.get(), 11) != SQLITE_NULL) {
                blob.link_next = static_cast<std::int64_t>(sqlite3_column_int64(select_blobs.get(), 11));
            }
            blob.source_mel_bins = sqlite3_column_int(select_blobs.get(), 12);
            blob.source_mel_frames = sqlite3_column_int(select_blobs.get(), 13);
            blob.original_pitch_curve = unpack_pitch_slice(read_blob_column(select_blobs.get(), 14));
            blob.source_audio_44k = unpack_float_vector(read_blob_column(select_blobs.get(), 15));
            blob.source_mel_log = unpack_float_vector(read_blob_column(select_blobs.get(), 16));
            blob.handdraw_patch_midi = unpack_float_vector(read_blob_column(select_blobs.get(), 17));
            blob.line_patches = unpack_line_patches(read_blob_column(select_blobs.get(), 18));
            track.blobs.push_back(std::move(blob));
        }

        out.tracks.push_back(std::move(track));
    }

    return out;
}

} // namespace melodick::project
