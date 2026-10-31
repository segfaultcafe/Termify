#include "LocalStore.hpp"

#include <algorithm>
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
    {
      Statement version(_db, "PRAGMA user_version");
      version.Next();
      if (version.Number(0) > 1)
        throw std::runtime_error(
            "Local library was created by a newer Termify version");
    }
    execute(_db, "PRAGMA journal_mode=WAL");
    execute(_db, "PRAGMA synchronous=NORMAL");
    execute(_db,
            "CREATE TABLE IF NOT EXISTS history (id INTEGER PRIMARY KEY, "
            "source TEXT NOT NULL,"
            "title TEXT NOT NULL, artist TEXT NOT NULL, album TEXT NOT NULL,"
            "duration INTEGER NOT NULL, played_at INTEGER NOT NULL);"
            "CREATE TABLE IF NOT EXISTS audio_cache (source TEXT PRIMARY KEY,"
            "filename TEXT UNIQUE NOT NULL, bytes INTEGER NOT NULL, touched "
            "INTEGER NOT NULL);"
            "PRAGMA user_version=1;");
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
      "c.filename FROM history h LEFT JOIN audio_cache c ON c.source=h.source"
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
  std::string filename;
  std::int64_t expectedSize = 0;
  {
    Statement query(_db,
                    "SELECT filename,bytes FROM audio_cache WHERE source=?");
    query.Text(1, source);
    if (!query.Next())
      return std::nullopt;
    filename = query.Text(0);
    expectedSize = query.Number(1);
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
           " FROM audio_cache) WHERE source=?");
  touch.Text(1, source);
  touch.Next();
  return CachedAudio{path.string(), std::move(lease)};
}

std::optional<CachedAudio> LocalStore::FindAudio(const std::string &source) {
  std::lock_guard lock(_mutex);
  return findAudio(source);
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
                       const std::filesystem::path &file) {
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
        "INSERT OR REPLACE INTO audio_cache(source,filename,bytes,touched)"
        " VALUES(?,?,?,(SELECT COALESCE(MAX(touched),0)+1 FROM audio_cache))");
    insert.Text(1, source);
    insert.Text(2, std::filesystem::path(pattern).filename().string());
    insert.Number(3, static_cast<std::int64_t>(bytes));
    insert.Next();
    transaction.Commit();
    ++_revision;
    return CachedAudio{pattern, std::move(lease)};
  } catch (...) {
    std::error_code error;
    std::filesystem::remove(pattern, error);
    throw;
  }
}

} // namespace termify::core
