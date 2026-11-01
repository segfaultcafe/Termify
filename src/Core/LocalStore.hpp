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

struct Playlist {
  std::int64_t id{0};
  std::string name;
  bool shuffle{false};
  bool loop{false};
  std::size_t trackCount{0};
};

struct PlaylistTrack {
  std::int64_t id{0};
  std::string sourceHash, source, title, artist, album;
  std::int64_t durationSeconds{0};
  int position{0};
  bool cached{false};
};

enum class AddTrackResult {
  Added,
  HistoryNotFound,
  NotCached,
  UnresolvedSource,
  Duplicate,
  PlaylistFull,
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
  // The canonical media URL reported by yt-dlp. Old cache rows fall back to
  // the original query so upgrades remain playable.
  std::string source;
  // Provider metadata saved with the cached audio. Empty on legacy rows until
  // the track is resolved or played again.
  std::string title;
  std::shared_ptr<CacheLease> lease;
};

class LocalStore {
public:
  static constexpr std::uint64_t CACHE_LIMIT = 256ULL * 1024 * 1024;
  static constexpr int HISTORY_LIMIT = 1000;
  static constexpr int PLAYLIST_TRACK_LIMIT = 20;
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
  std::optional<CachedAudio>
  StoreAudio(const std::string &source, const std::filesystem::path &file,
             const std::string &canonicalSource = {},
             const std::string &title = {});
  bool UpdateCachedSource(const std::string &source,
                          const std::string &canonicalSource);
  static std::string SourceHash(const std::string &source);

  std::vector<Playlist> Playlists();
  std::vector<PlaylistTrack> PlaylistTracks(std::int64_t playlistId);
  std::int64_t CreatePlaylist(const std::string &name);
  bool RenamePlaylist(std::int64_t playlistId, const std::string &name);
  bool DeletePlaylist(std::int64_t playlistId);
  bool SetPlaylistShuffle(std::int64_t playlistId, bool enabled);
  bool SetPlaylistLoop(std::int64_t playlistId, bool enabled);
  AddTrackResult AddHistoryToPlaylist(std::int64_t playlistId,
                                      std::int64_t historyId);
  AddTrackResult AddSourceToPlaylist(std::int64_t playlistId,
                                     const HistoryEntry &track);
  AddTrackResult AddVerifiedSourceToPlaylist(std::int64_t playlistId,
                                             const HistoryEntry &track);
  bool RemovePlaylistTrack(std::int64_t playlistId, std::int64_t entryId);
  bool MovePlaylistTrack(std::int64_t playlistId, std::int64_t entryId,
                         int direction);
  std::optional<std::string> TrackTitle(const std::string &source);
  std::optional<int> SavedVolume();
  void SaveVolume(int volume);
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
