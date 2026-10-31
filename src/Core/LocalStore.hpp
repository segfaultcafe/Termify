#ifndef TERMIFY_CORE_LOCAL_STORE_HPP
#define TERMIFY_CORE_LOCAL_STORE_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct sqlite3;

namespace termify::core {

struct HistoryEntry {
  std::int64_t id{0};
  std::string source, title, artist, album;
  std::int64_t durationSeconds{0}, playedAt{0};
  bool cached{false};
};

// Shared by the queue and decoder. An advisory lock prevents eviction while
// any Termify process still has this cached file queued or playing.
struct CacheLease {
  explicit CacheLease(int descriptor) : fd(descriptor) {}
  ~CacheLease();
  CacheLease(const CacheLease &) = delete;
  CacheLease &operator=(const CacheLease &) = delete;
  int fd;
};

struct CachedAudio {
  std::string path;
  std::shared_ptr<CacheLease> lease;
};

class LocalStore {
public:
  static constexpr std::uint64_t CACHE_LIMIT = 256ULL * 1024 * 1024;
  static constexpr int HISTORY_LIMIT = 1000;
  static std::filesystem::path DefaultDataDirectory();
  static std::filesystem::path DefaultCacheDirectory();

  LocalStore(std::filesystem::path dataDirectory,
             std::filesystem::path cacheDirectory,
             std::uint64_t cacheLimit = CACHE_LIMIT);
  ~LocalStore();
  LocalStore(const LocalStore &) = delete;
  LocalStore &operator=(const LocalStore &) = delete;

  void Record(const HistoryEntry &entry);
  void QueueRecord(HistoryEntry entry);
  std::string TakeError();
  std::vector<HistoryEntry> History();
  std::optional<CachedAudio> FindAudio(const std::string &source);
  std::optional<CachedAudio> StoreAudio(const std::string &source,
                                        const std::filesystem::path &file);
  std::uint64_t Revision() const { return _revision.load(); }

private:
  std::optional<CachedAudio> findAudio(const std::string &source);
  bool trimCache(std::uint64_t target);
  std::filesystem::path cachePath(const std::string &filename) const;
  sqlite3 *_db{nullptr};
  std::filesystem::path _cacheDirectory;
  std::uint64_t _cacheLimit;
  std::mutex _mutex;
  std::atomic_uint64_t _revision{1};
  std::mutex _pendingMutex;
  std::condition_variable _pendingCv;
  std::deque<HistoryEntry> _pending;
  std::string _lastError;
  bool _closing{false};
  std::thread _writer;
};

} // namespace termify::core
#endif
