#ifndef _TERMIFY_CORE_SONG_QUEUE_HPP
#define _TERMIFY_CORE_SONG_QUEUE_HPP

#include "Defines.hpp"
#include "LocalStore.hpp"
#include <deque>
#include <optional>

namespace termify::core {

enum class QueueState { Pending, Downloading, Ready, Failed };

struct QueuedSong {
  uint64 id{0};
  string query;
  QueueState state{QueueState::Pending};
  string preparedPath;
  bool temporary{false};
  string error;
  std::shared_ptr<CacheLease> cacheLease;
};

class SongQueue {
public:
  static constexpr std::size_t MAX_SONGS = 20;

  std::optional<uint64> Push(string song);
  std::optional<QueuedSong> PopForPlayback();
  std::optional<QueuedSong> ClaimForPrefetch();
  bool MarkReady(uint64 id, string path, bool temporary,
                 std::shared_ptr<CacheLease> lease = {});
  bool MarkFailed(uint64 id, string error);
  vector<QueuedSong> Snapshot(std::size_t limit = 3) const;
  std::size_t Size() const;
  bool Empty() const;
  bool ReadyForPlayback() const;
  bool HasPending() const;
  vector<QueuedSong> Clear();

private:
  std::deque<QueuedSong> _queue;
  uint64 _nextId{1};
  mutable std::mutex _queueMtx;
};

} // namespace termify::core

#endif // _TERMIFY_CORE_SONG_QUEUE_HPP
