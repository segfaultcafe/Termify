#include "SongQueue.hpp"
#include <mutex>

namespace termify::core {

namespace {
QueuedSong makeQueuedSong(uint64 id, string song, QueueOrigin origin) {
  QueuedSong queued;
  queued.id = id;
  queued.query = std::move(song);
  queued.origin = origin;
  return queued;
}
} // namespace

std::optional<uint64> SongQueue::Push(string song) {
  std::lock_guard lock(_queueMtx);
  if (song.empty()) {
    return std::nullopt;
  }
  const auto duplicate =
      std::find_if(_queue.begin(), _queue.end(), [&](const auto &queued) {
        return queued.query == song ||
               (!queued.source.empty() && queued.source == song);
      });
  if (duplicate != _queue.end())
    return duplicate->id;
  if (_queue.size() >= MAX_SONGS)
    return std::nullopt;
  const uint64 id = _nextId++;
  _queue.push_back(makeQueuedSong(id, std::move(song), QueueOrigin::Manual));
  return id;
}

std::optional<uint64> SongQueue::PushNext(string song) {
  return PrioritizeNext(std::move(song)).id;
}

QueueNextOutcome SongQueue::PrioritizeNext(string song) {
  std::lock_guard lock(_queueMtx);
  if (song.empty())
    return {QueueNextResult::Invalid, std::nullopt};
  const auto matches = [&](const auto &queued) {
    return queued.query == song ||
           (!queued.source.empty() && queued.source == song);
  };
  if (!_queue.empty() && matches(_queue.front()))
    return {QueueNextResult::AlreadyNext, _queue.front().id};
  const auto duplicate = std::find_if(_queue.begin(), _queue.end(), matches);
  if (duplicate != _queue.end()) {
    QueuedSong queued = std::move(*duplicate);
    queued.origin = QueueOrigin::Manual;
    _queue.erase(duplicate);
    const auto id = queued.id;
    _queue.push_front(std::move(queued));
    return {QueueNextResult::Moved, id};
  }
  if (_queue.size() >= MAX_SONGS)
    return {QueueNextResult::Full, std::nullopt};
  const uint64 id = _nextId++;
  _queue.push_front(makeQueuedSong(id, std::move(song), QueueOrigin::Manual));
  return {QueueNextResult::Added, id};
}

bool SongQueue::PushPlaylistCycle(vector<string> songs) {
  std::lock_guard lock(_queueMtx);
  if (songs.empty() || !_queue.empty() || songs.size() > MAX_SONGS ||
      std::any_of(songs.begin(), songs.end(),
                  [](const auto &song) { return song.empty(); })) {
    return false;
  }
  for (auto &song : songs) {
    const uint64 id = _nextId++;
    _queue.push_back(
        makeQueuedSong(id, std::move(song), QueueOrigin::Playlist));
  }
  return true;
}

std::optional<QueuedSong> SongQueue::PopForPlayback() {
  std::lock_guard lock(_queueMtx);
  if (_queue.empty() || _queue.front().state == QueueState::Downloading) {
    return std::nullopt;
  }
  QueuedSong song = std::move(_queue.front());
  _queue.pop_front();
  return song;
}

std::optional<QueuedSong> SongQueue::ClaimForPrefetch() {
  std::lock_guard lock(_queueMtx);
  for (auto &song : _queue) {
    if (song.state == QueueState::Pending) {
      song.state = QueueState::Downloading;
      return song;
    }
  }
  return std::nullopt;
}

bool SongQueue::MarkReady(uint64 id, string path, string source, bool temporary,
                          std::shared_ptr<CacheLease> lease) {
  std::lock_guard lock(_queueMtx);
  const auto song =
      std::find_if(_queue.begin(), _queue.end(),
                   [id](const auto &item) { return item.id == id; });
  if (song == _queue.end()) {
    return false;
  }
  song->state = QueueState::Ready;
  song->preparedPath = std::move(path);
  song->source = std::move(source);
  song->temporary = temporary;
  song->cacheLease = std::move(lease);
  song->error.clear();
  return true;
}

bool SongQueue::MarkFailed(uint64 id, string error) {
  std::lock_guard lock(_queueMtx);
  const auto song =
      std::find_if(_queue.begin(), _queue.end(),
                   [id](const auto &item) { return item.id == id; });
  if (song == _queue.end()) {
    return false;
  }
  song->state = QueueState::Failed;
  song->error = std::move(error);
  song->preparedPath.clear();
  song->temporary = false;
  song->cacheLease.reset();
  return true;
}

vector<QueuedSong> SongQueue::Snapshot(std::size_t limit) const {
  std::lock_guard lock(_queueMtx);
  const std::size_t count = std::min(limit, _queue.size());
  return vector<QueuedSong>(_queue.begin(), _queue.begin() + count);
}

std::size_t SongQueue::Size() const {
  std::lock_guard lock(_queueMtx);
  return _queue.size();
}

bool SongQueue::Empty() const {
  std::lock_guard lock(_queueMtx);
  return _queue.empty();
}

bool SongQueue::ReadyForPlayback() const {
  std::lock_guard lock(_queueMtx);
  return !_queue.empty() && _queue.front().state != QueueState::Downloading;
}

bool SongQueue::HasPending() const {
  std::lock_guard lock(_queueMtx);
  return std::any_of(_queue.begin(), _queue.end(), [](const auto &song) {
    return song.state == QueueState::Pending;
  });
}

vector<QueuedSong> SongQueue::Clear() {
  std::lock_guard lock(_queueMtx);
  vector<QueuedSong> removed(_queue.begin(), _queue.end());
  _queue.clear();
  return removed;
}

} // namespace termify::core
