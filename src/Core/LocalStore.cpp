#include "LocalStore.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <set>
#include <sqlite3.h>
#include <stdexcept>
#include <sys/file.h>
#include <unistd.h>
#include <utility>

namespace termify::core {
namespace {
void execute(sqlite3 *db, const char *sql) {
  if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db));
}

class Statement {
public:
  Statement(sqlite3 *db, const char *sql) : _db(db) {
    if (sqlite3_prepare_v2(db, sql, -1, &_statement, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db));
  }
  ~Statement() { sqlite3_finalize(_statement); }
  void Text(int column, const std::string &value) {
    if (sqlite3_bind_text(_statement, column, value.c_str(),
                          static_cast<int>(value.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(_db));
  }
  void Number(int column, std::int64_t value) {
    if (sqlite3_bind_int64(_statement, column, value) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(_db));
  }
  bool Next() {
    const int result = sqlite3_step(_statement);
    if (result != SQLITE_ROW && result != SQLITE_DONE)
      throw std::runtime_error(sqlite3_errmsg(_db));
    return result == SQLITE_ROW;
  }
  std::string Text(int column) const {
    const auto *value = sqlite3_column_text(_statement, column);
    return value ? reinterpret_cast<const char *>(value) : "";
  }
  std::int64_t Number(int column) const {
    return sqlite3_column_int64(_statement, column);
  }

private:
  sqlite3 *_db;
  sqlite3_stmt *_statement{nullptr};
};

class Transaction {
public:
  explicit Transaction(sqlite3 *db) : _db(db) {
    execute(db, "BEGIN IMMEDIATE");
  }
  ~Transaction() {
    if (!_committed)
      sqlite3_exec(_db, "ROLLBACK", nullptr, nullptr, nullptr);
  }
  void Commit() {
    execute(_db, "COMMIT");
    _committed = true;
  }

private:
  sqlite3 *_db;
  bool _committed{false};
};

std::filesystem::path xdgDirectory(const char *variable, const char *fallback) {
  if (const char *value = std::getenv(variable);
      value && *value && std::filesystem::path(value).is_absolute())
    return std::filesystem::path(value) / "termify";
  const char *home = std::getenv("HOME");
  if (!home || !*home || !std::filesystem::path(home).is_absolute())
    throw std::runtime_error(
        "HOME or an absolute XDG directory is required for storage");
  return std::filesystem::path(home) / fallback / "termify";
}

std::shared_ptr<CacheLease> lockFile(const std::filesystem::path &path,
                                     int mode) {
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return {};
  if (flock(fd, mode | LOCK_NB) != 0) {
    close(fd);
    return {};
  }
  return std::make_shared<CacheLease>(fd);
}

std::string playlistName(std::string value) {
  const auto visible = [](unsigned char character) {
    return !std::isspace(character);
  };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), visible));
  value.erase(std::find_if(value.rbegin(), value.rend(), visible).base(),
              value.end());
  value.erase(
      std::remove_if(value.begin(), value.end(),
                     [](unsigned char ch) { return ch < 32 || ch == 127; }),
      value.end());
  if (value.empty() || value.size() > 80)
    throw std::runtime_error("Playlist names must be 1 to 80 characters");
  return value;
}
} // namespace

CacheLease::~CacheLease() { close(fd); }

std::filesystem::path LocalStore::DefaultDataDirectory() {
  return xdgDirectory("XDG_DATA_HOME", ".local/share");
}
std::filesystem::path LocalStore::DefaultCacheDirectory() {
  return xdgDirectory("XDG_CACHE_HOME", ".cache") / "audio";
}

LocalStore::LocalStore(std::filesystem::path dataDirectory,
                       std::filesystem::path cacheDirectory,
                       std::uint64_t cacheLimit)
    : _cacheDirectory(std::move(cacheDirectory)), _cacheLimit(cacheLimit) {
  std::filesystem::create_directories(dataDirectory);
  std::filesystem::create_directories(_cacheDirectory);
  std::filesystem::permissions(dataDirectory,
                               std::filesystem::perms::owner_all);
  std::filesystem::permissions(_cacheDirectory,
                               std::filesystem::perms::owner_all);
  const auto database = dataDirectory / "library.sqlite3";
  if (sqlite3_open_v2(database.c_str(), &_db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    const std::string error =
        _db ? sqlite3_errmsg(_db) : "Could not open local storage";
    sqlite3_close(_db);
    _db = nullptr;
    throw std::runtime_error(error);
  }
  try {
    sqlite3_busy_timeout(_db, 1000);
    std::filesystem::permissions(database,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write);
    std::int64_t schemaVersion = 0;
    {
      Statement version(_db, "PRAGMA user_version");
      version.Next();
      schemaVersion = version.Number(0);
    }
    if (schemaVersion > 2)
      throw std::runtime_error(
          "Local library was created by a newer Termify version");
    execute(_db, "PRAGMA journal_mode=WAL");
    execute(_db, "PRAGMA synchronous=NORMAL");
    execute(_db, "PRAGMA foreign_keys=ON");
    {
      Transaction migration(_db);
      execute(_db,
              "CREATE TABLE IF NOT EXISTS history (id INTEGER PRIMARY KEY, "
              "source TEXT NOT NULL,"
              "title TEXT NOT NULL, artist TEXT NOT NULL, album TEXT NOT NULL,"
              "duration INTEGER NOT NULL, played_at INTEGER NOT NULL);"
              "CREATE TABLE IF NOT EXISTS audio_cache (source TEXT PRIMARY KEY,"
              "filename TEXT UNIQUE NOT NULL, bytes INTEGER NOT NULL, touched "
              "INTEGER NOT NULL, canonical_source TEXT NOT NULL DEFAULT '');");
      if (schemaVersion == 1) {
        execute(_db,
                "ALTER TABLE audio_cache ADD COLUMN canonical_source TEXT NOT "
                "NULL DEFAULT '';"
                "UPDATE audio_cache SET canonical_source=source WHERE "
                "canonical_source='';");
      }
      execute(
          _db,
          "CREATE INDEX IF NOT EXISTS audio_cache_canonical ON "
          "audio_cache(canonical_source);"
          "CREATE TABLE IF NOT EXISTS library_tracks ("
          "source_hash TEXT PRIMARY KEY, source TEXT UNIQUE NOT NULL,"
          "title TEXT NOT NULL, artist TEXT NOT NULL, album TEXT NOT NULL,"
          "duration INTEGER NOT NULL);"
          "CREATE TABLE IF NOT EXISTS playlists ("
          "id INTEGER PRIMARY KEY, name TEXT NOT NULL COLLATE NOCASE UNIQUE,"
          "shuffle INTEGER NOT NULL DEFAULT 0,"
          "loop INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL);"
          "CREATE TABLE IF NOT EXISTS playlist_entries ("
          "id INTEGER PRIMARY KEY, playlist_id INTEGER NOT NULL REFERENCES "
          "playlists(id) ON DELETE CASCADE, source_hash TEXT NOT NULL "
          "REFERENCES library_tracks(source_hash), position INTEGER NOT "
          "NULL, UNIQUE(playlist_id,source_hash),"
          "UNIQUE(playlist_id,position));"
          "PRAGMA user_version=2;");
      migration.Commit();
    }
    Transaction transaction(_db);
    trimCache(_cacheLimit);
    // Reclaim only our unindexed files left by an interrupted cache write.
    // The write transaction excludes other instances publishing new entries.
    std::set<std::string> indexed;
    {
      Statement files(_db, "SELECT filename FROM audio_cache");
      while (files.Next())
        indexed.insert(files.Text(0));
    }
    for (const auto &file :
         std::filesystem::directory_iterator(_cacheDirectory)) {
      const auto name = file.path().filename().string();
      if (!name.starts_with("audio-") || indexed.contains(name))
        continue;
      if (auto exclusive = lockFile(file.path(), LOCK_EX)) {
        std::error_code error;
        std::filesystem::remove(file.path(), error);
      }
    }
    transaction.Commit();
    _writer = std::thread([this] {
      std::unique_lock lock(_pendingMutex);
      while (true) {
        _pendingCv.wait(lock, [this] { return _closing || !_pending.empty(); });
        if (_pending.empty() && _closing)
          return;
        auto entry = std::move(_pending.front());
        _pending.pop_front();
        lock.unlock();
        try {
          Record(entry);
        } catch (const std::exception &error) {
          std::lock_guard errorLock(_pendingMutex);
          _lastError = error.what();
        }
        lock.lock();
      }
    });
  } catch (...) {
    sqlite3_close(_db);
    _db = nullptr;
    throw;
  }
}

LocalStore::~LocalStore() {
  {
    std::lock_guard lock(_pendingMutex);
    _closing = true;
  }
  _pendingCv.notify_all();
  if (_writer.joinable())
    _writer.join();
  sqlite3_close(_db);
}

void LocalStore::QueueRecord(HistoryEntry entry) {
  std::lock_guard lock(_pendingMutex);
  if (_pending.size() < HISTORY_LIMIT)
    _pending.push_back(std::move(entry));
  else
    _lastError = "History writer is busy; a play could not be saved";
  _pendingCv.notify_one();
}

std::string LocalStore::TakeError() {
  std::lock_guard lock(_pendingMutex);
  return std::exchange(_lastError, {});
}

void LocalStore::Record(const HistoryEntry &entry) {
  std::lock_guard lock(_mutex);
  Transaction transaction(_db);
  Statement insert(
      _db, "INSERT INTO history(source,title,artist,album,duration,played_at)"
           " VALUES(?,?,?,?,?,?)");
  insert.Text(1, entry.source);
  insert.Text(2, entry.title.empty() ? entry.source : entry.title);
  insert.Text(3, entry.artist);
  insert.Text(4, entry.album);
  insert.Number(5, std::max<std::int64_t>(0, entry.durationSeconds));
  insert.Number(6, entry.playedAt ? entry.playedAt : std::time(nullptr));
  insert.Next();
  Statement trim(_db, "DELETE FROM history WHERE id NOT IN"
                      " (SELECT id FROM history ORDER BY id DESC LIMIT ?)");
  trim.Number(1, HISTORY_LIMIT);
  trim.Next();
  transaction.Commit();
  ++_revision;
}

std::vector<HistoryEntry> LocalStore::History() {
  std::lock_guard lock(_mutex);
  Statement query(
      _db,
      "SELECT h.id,h.source,h.title,h.artist,h.album,h.duration,h.played_at,"
      "COALESCE((SELECT c.filename FROM audio_cache c WHERE c.source=h.source "
      "OR c.canonical_source=h.source LIMIT 1),'') FROM history h"
      " ORDER BY h.id DESC");
  std::vector<HistoryEntry> entries;
  while (query.Next()) {
    const auto file = cachePath(query.Text(7));
    std::error_code error;
    entries.push_back(
        {query.Number(0), query.Text(1), query.Text(2), query.Text(3),
         query.Text(4), query.Number(5), query.Number(6),
         !file.empty() && std::filesystem::is_regular_file(file, error)});
  }
  return entries;
}

std::filesystem::path LocalStore::cachePath(const std::string &filename) const {
  if (!filename.starts_with("audio-") ||
      std::filesystem::path(filename).filename() != filename)
    return {};
  return _cacheDirectory / filename;
}

std::optional<CachedAudio> LocalStore::findAudio(const std::string &source) {
  std::string filename, canonicalSource;
  std::int64_t expectedSize = 0;
  {
    Statement query(_db,
                    "SELECT filename,bytes,canonical_source FROM audio_cache "
                    "WHERE source=? OR canonical_source=? ORDER BY source=? "
                    "DESC LIMIT 1");
    query.Text(1, source);
    query.Text(2, source);
    query.Text(3, source);
    if (!query.Next())
      return std::nullopt;
    filename = query.Text(0);
    expectedSize = query.Number(1);
    canonicalSource = query.Text(2);
  }
  const auto path = cachePath(filename);
  auto lease = path.empty() ? nullptr : lockFile(path, LOCK_SH);
  std::error_code error;
  if (!lease || expectedSize <= 0 ||
      std::filesystem::file_size(path, error) !=
          static_cast<std::uint64_t>(expectedSize) ||
      error)
    return std::nullopt;
  Statement touch(
      _db, "UPDATE audio_cache SET touched=(SELECT COALESCE(MAX(touched),0)+1"
           " FROM audio_cache) WHERE source=? OR canonical_source=?");
  touch.Text(1, source);
  touch.Text(2, source);
  touch.Next();
  return CachedAudio{path.string(),
                     canonicalSource.empty() ? source : canonicalSource,
                     std::move(lease)};
}

std::vector<Playlist> LocalStore::Playlists() {
  std::lock_guard lock(_mutex);
  Statement query(
      _db, "SELECT p.id,p.name,p.shuffle,p.loop,COUNT(e.id) FROM playlists p "
           "LEFT JOIN playlist_entries e ON e.playlist_id=p.id GROUP BY p.id "
           "ORDER BY p.created_at,p.id");
  std::vector<Playlist> playlists;
  while (query.Next())
    playlists.push_back({query.Number(0), query.Text(1), query.Number(2) != 0,
                         query.Number(3) != 0,
                         static_cast<std::size_t>(query.Number(4))});
  return playlists;
}

std::vector<PlaylistTrack> LocalStore::PlaylistTracks(std::int64_t playlistId) {
  std::lock_guard lock(_mutex);
  Statement query(
      _db,
      "SELECT e.id,t.source_hash,t.source,t.title,t.artist,t.album,t.duration,"
      "e.position,COALESCE((SELECT c.filename FROM audio_cache c WHERE "
      "c.source=t.source OR c.canonical_source=t.source LIMIT 1),'') FROM "
      "playlist_entries e JOIN library_tracks t ON t.source_hash=e.source_hash "
      "WHERE e.playlist_id=? ORDER BY e.position");
  query.Number(1, playlistId);
  std::vector<PlaylistTrack> tracks;
  while (query.Next()) {
    const auto file = cachePath(query.Text(8));
    std::error_code error;
    tracks.push_back(
        {query.Number(0), query.Text(1), query.Text(2), query.Text(3),
         query.Text(4), query.Text(5), query.Number(6),
         static_cast<int>(query.Number(7)),
         !file.empty() && std::filesystem::is_regular_file(file, error)});
  }
  return tracks;
}

std::int64_t LocalStore::CreatePlaylist(const std::string &name) {
  const auto cleaned = playlistName(name);
  std::lock_guard lock(_mutex);
  Statement insert(_db, "INSERT INTO playlists(name,created_at) VALUES(?,?)");
  insert.Text(1, cleaned);
  insert.Number(2, std::time(nullptr));
  insert.Next();
  ++_revision;
  return sqlite3_last_insert_rowid(_db);
}

bool LocalStore::RenamePlaylist(std::int64_t playlistId,
                                const std::string &name) {
  const auto cleaned = playlistName(name);
  std::lock_guard lock(_mutex);
  Statement update(_db, "UPDATE playlists SET name=? WHERE id=?");
  update.Text(1, cleaned);
  update.Number(2, playlistId);
  update.Next();
  const bool changed = sqlite3_changes(_db) != 0;
  if (changed)
    ++_revision;
  return changed;
}

bool LocalStore::DeletePlaylist(std::int64_t playlistId) {
  std::lock_guard lock(_mutex);
  Statement remove(_db, "DELETE FROM playlists WHERE id=?");
  remove.Number(1, playlistId);
  remove.Next();
  const bool changed = sqlite3_changes(_db) != 0;
  if (changed)
    ++_revision;
  return changed;
}

bool LocalStore::SetPlaylistShuffle(std::int64_t playlistId, bool enabled) {
  std::lock_guard lock(_mutex);
  Statement update(_db, "UPDATE playlists SET shuffle=? WHERE id=?");
  update.Number(1, enabled ? 1 : 0);
  update.Number(2, playlistId);
  update.Next();
  const bool changed = sqlite3_changes(_db) != 0;
  if (changed)
    ++_revision;
  return changed;
}

bool LocalStore::SetPlaylistLoop(std::int64_t playlistId, bool enabled) {
  std::lock_guard lock(_mutex);
  Statement update(_db, "UPDATE playlists SET loop=? WHERE id=?");
  update.Number(1, enabled ? 1 : 0);
  update.Number(2, playlistId);
  update.Next();
  const bool changed = sqlite3_changes(_db) != 0;
  if (changed)
    ++_revision;
  return changed;
}

AddTrackResult LocalStore::AddHistoryToPlaylist(std::int64_t playlistId,
                                                std::int64_t historyId) {
  HistoryEntry history;
  {
    std::lock_guard lock(_mutex);
    Statement query(
        _db, "SELECT id,source,title,artist,album,duration,played_at FROM "
             "history WHERE id=?");
    query.Number(1, historyId);
    if (!query.Next())
      return AddTrackResult::HistoryNotFound;
    history = {query.Number(0), query.Text(1),   query.Text(2),   query.Text(3),
               query.Text(4),   query.Number(5), query.Number(6), false};
  }
  return AddSourceToPlaylist(playlistId, history);
}

AddTrackResult LocalStore::AddSourceToPlaylist(std::int64_t playlistId,
                                               const HistoryEntry &track) {
  std::lock_guard lock(_mutex);
  std::string canonical, filename;
  {
    Statement cached(
        _db, "SELECT canonical_source,filename FROM audio_cache WHERE source=? "
             "OR canonical_source=? ORDER BY source=? DESC LIMIT 1");
    cached.Text(1, track.source);
    cached.Text(2, track.source);
    cached.Text(3, track.source);
    if (!cached.Next())
      return AddTrackResult::NotCached;
    canonical = cached.Text(0).empty() ? track.source : cached.Text(0);
    filename = cached.Text(1);
  }
  std::error_code error;
  const auto file = cachePath(filename);
  if (file.empty() || !std::filesystem::is_regular_file(file, error))
    return AddTrackResult::NotCached;
  if (!canonical.starts_with("https://") && !canonical.starts_with("http://"))
    return AddTrackResult::UnresolvedSource;

  Statement heard(_db, "SELECT 1 FROM history WHERE source=? OR source=(SELECT "
                       "source FROM audio_cache WHERE canonical_source=? LIMIT "
                       "1) LIMIT 1");
  heard.Text(1, canonical);
  heard.Text(2, canonical);
  if (!heard.Next())
    return AddTrackResult::HistoryNotFound;

  Statement count(_db,
                  "SELECT COUNT(*) FROM playlist_entries WHERE playlist_id=?");
  count.Number(1, playlistId);
  count.Next();
  if (count.Number(0) >= PLAYLIST_TRACK_LIMIT)
    return AddTrackResult::PlaylistFull;

  const auto hash = SourceHash(canonical);
  Transaction transaction(_db);
  Statement upsert(
      _db,
      "INSERT INTO library_tracks(source_hash,source,title,artist,album,"
      "duration) VALUES(?,?,?,?,?,?) ON CONFLICT(source_hash) DO UPDATE "
      "SET title=excluded.title,artist=excluded.artist,album=excluded.album,"
      "duration=excluded.duration");
  upsert.Text(1, hash);
  upsert.Text(2, canonical);
  upsert.Text(3, track.title.empty() ? canonical : track.title);
  upsert.Text(4, track.artist);
  upsert.Text(5, track.album);
  upsert.Number(6, std::max<std::int64_t>(0, track.durationSeconds));
  upsert.Next();
  Statement insert(
      _db, "INSERT OR IGNORE INTO playlist_entries(playlist_id,source_hash,"
           "position) SELECT ?,?,COALESCE(MAX(position),-1)+1 FROM "
           "playlist_entries WHERE playlist_id=?");
  insert.Number(1, playlistId);
  insert.Text(2, hash);
  insert.Number(3, playlistId);
  insert.Next();
  const bool added = sqlite3_changes(_db) != 0;
  transaction.Commit();
  if (added)
    ++_revision;
  return added ? AddTrackResult::Added : AddTrackResult::Duplicate;
}

bool LocalStore::RemovePlaylistTrack(std::int64_t playlistId,
                                     std::int64_t entryId) {
  std::lock_guard lock(_mutex);
  Transaction transaction(_db);
  int position = -1;
  {
    Statement query(_db, "SELECT position FROM playlist_entries WHERE id=? AND "
                         "playlist_id=?");
    query.Number(1, entryId);
    query.Number(2, playlistId);
    if (!query.Next()) {
      transaction.Commit();
      return false;
    }
    position = static_cast<int>(query.Number(0));
  }
  Statement remove(_db, "DELETE FROM playlist_entries WHERE id=?");
  remove.Number(1, entryId);
  remove.Next();
  Statement compact(_db, "UPDATE playlist_entries SET position=position-1 "
                         "WHERE playlist_id=? AND position>?");
  compact.Number(1, playlistId);
  compact.Number(2, position);
  compact.Next();
  transaction.Commit();
  ++_revision;
  return true;
}

bool LocalStore::MovePlaylistTrack(std::int64_t playlistId,
                                   std::int64_t entryId, int direction) {
  if (direction != -1 && direction != 1)
    return false;
  std::lock_guard lock(_mutex);
  Transaction transaction(_db);
  int position = -1;
  {
    Statement query(_db, "SELECT position FROM playlist_entries WHERE id=? AND "
                         "playlist_id=?");
    query.Number(1, entryId);
    query.Number(2, playlistId);
    if (!query.Next()) {
      transaction.Commit();
      return false;
    }
    position = static_cast<int>(query.Number(0));
  }
  const int target = position + direction;
  Statement other(_db, "SELECT id FROM playlist_entries WHERE playlist_id=? "
                       "AND position=?");
  other.Number(1, playlistId);
  other.Number(2, target);
  if (!other.Next()) {
    transaction.Commit();
    return false;
  }
  const auto otherId = other.Number(0);
  Statement temporary(_db,
                      "UPDATE playlist_entries SET position=-1 WHERE id=?");
  temporary.Number(1, entryId);
  temporary.Next();
  Statement moveOther(_db, "UPDATE playlist_entries SET position=? WHERE id=?");
  moveOther.Number(1, position);
  moveOther.Number(2, otherId);
  moveOther.Next();
  Statement moveSelected(_db,
                         "UPDATE playlist_entries SET position=? WHERE id=?");
  moveSelected.Number(1, target);
  moveSelected.Number(2, entryId);
  moveSelected.Next();
  transaction.Commit();
  ++_revision;
  return true;
}

std::optional<CachedAudio> LocalStore::FindAudio(const std::string &source) {
  std::lock_guard lock(_mutex);
  return findAudio(source);
}

bool LocalStore::UpdateCachedSource(const std::string &source,
                                    const std::string &canonicalSource) {
  if ((!canonicalSource.starts_with("https://") &&
       !canonicalSource.starts_with("http://")) ||
      source.empty())
    return false;
  std::lock_guard lock(_mutex);
  Statement update(_db, "UPDATE audio_cache SET canonical_source=? WHERE "
                        "source=?");
  update.Text(1, canonicalSource);
  update.Text(2, source);
  update.Next();
  const bool changed = sqlite3_changes(_db) != 0;
  if (changed)
    ++_revision;
  return changed;
}

bool LocalStore::trimCache(std::uint64_t target) {
  struct File {
    std::string source, filename;
    std::uint64_t size;
  };
  std::vector<File> files;
  std::uint64_t total = 0;
  {
    Statement query(
        _db,
        "SELECT source,filename,bytes FROM audio_cache ORDER BY touched,rowid");
    while (query.Next()) {
      const auto size = static_cast<std::uint64_t>(
          std::max<std::int64_t>(0, query.Number(2)));
      files.push_back({query.Text(0), query.Text(1), size});
      total += size;
    }
  }
  for (const auto &file : files) {
    const auto path = cachePath(file.filename);
    std::error_code error;
    const bool exists = !path.empty() && std::filesystem::exists(path, error);
    if (error || (exists && total <= target))
      continue;
    auto exclusive = exists ? lockFile(path, LOCK_EX) : nullptr;
    if (exists && !exclusive)
      continue;
    if (exists && !std::filesystem::remove(path, error))
      continue;
    Statement remove(_db, "DELETE FROM audio_cache WHERE source=?");
    remove.Text(1, file.source);
    remove.Next();
    total -= file.size;
    ++_revision;
  }
  return total <= target;
}

std::optional<CachedAudio>
LocalStore::StoreAudio(const std::string &source,
                       const std::filesystem::path &file,
                       const std::string &canonicalSource) {
  std::lock_guard lock(_mutex);
  Transaction transaction(_db);
  if (auto existing = findAudio(source)) {
    transaction.Commit();
    return existing;
  }
  // Remove stale/corrupt entries before replacing them; never orphan an old
  // cached file or overwrite a file which another player has leased.
  std::string stale;
  {
    Statement query(_db, "SELECT filename FROM audio_cache WHERE source=?");
    query.Text(1, source);
    if (query.Next())
      stale = query.Text(0);
  }
  if (!stale.empty()) {
    const auto path = cachePath(stale);
    if (!path.empty() && std::filesystem::exists(path)) {
      auto exclusive = lockFile(path, LOCK_EX);
      if (!exclusive) {
        transaction.Commit();
        return std::nullopt;
      }
      std::filesystem::remove(path);
    }
    Statement remove(_db, "DELETE FROM audio_cache WHERE source=?");
    remove.Text(1, source);
    remove.Next();
  }
  const auto bytes = std::filesystem::file_size(file);
  if (bytes == 0 || bytes > _cacheLimit || !trimCache(_cacheLimit - bytes)) {
    transaction.Commit();
    return std::nullopt;
  }
  std::string pattern = (_cacheDirectory / "audio-XXXXXX").string();
  const int descriptor = mkstemp(pattern.data());
  if (descriptor < 0)
    throw std::runtime_error("Could not create an audio cache file");
  auto lease = std::make_shared<CacheLease>(descriptor);
  fcntl(descriptor, F_SETFD, FD_CLOEXEC);
  if (flock(descriptor, LOCK_SH) != 0) {
    std::filesystem::remove(pattern);
    throw std::runtime_error("Could not lock an audio cache file");
  }
  try {
    std::filesystem::copy_file(
        file, pattern, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::permissions(pattern,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write);
    Statement insert(
        _db,
        "INSERT OR REPLACE INTO audio_cache(source,filename,bytes,touched,"
        "canonical_source) VALUES(?,?,?,(SELECT COALESCE(MAX(touched),0)+1 "
        "FROM audio_cache),?)");
    insert.Text(1, source);
    insert.Text(2, std::filesystem::path(pattern).filename().string());
    insert.Number(3, static_cast<std::int64_t>(bytes));
    insert.Text(4, canonicalSource.empty() ? source : canonicalSource);
    insert.Next();
    transaction.Commit();
    ++_revision;
    return CachedAudio{pattern,
                       canonicalSource.empty() ? source : canonicalSource,
                       std::move(lease)};
  } catch (...) {
    std::error_code error;
    std::filesystem::remove(pattern, error);
    throw;
  }
}

} // namespace termify::core
