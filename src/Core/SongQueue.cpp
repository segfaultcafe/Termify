#include "SongQueue.hpp"
#include <mutex>

namespace termify::core {

namespace {
QueuedSong makeQueuedSong(uint64 id, string song, string title,
                          QueueOrigin origin) {
  QueuedSong queued;
  queued.id = id;
  queued.query = std::move(song);
  queued.title = std::move(title);
  queued.origin = origin;
  return queued;
}
} // namespace

std::optional<uint64> SongQueue::Push(string song, string title) {
  std::lock_guard lock(_queueMtx);
  if (song.empty()) {
    return std::nullopt;
  }
  const auto duplicate =
      std::find_if(_queue.begin(), _queue.end(), [&](const auto &queued) {
        return queued.query == song ||
               (!queued.source.empty() && queued.source == song);
      });
  if (duplicate != _queue.end()) {
    if (!title.empty())
      duplicate->title = std::move(title);
    return duplicate->id;
  }
  if (_queue.size() >= MAX_SONGS)
    return std::nullopt;
  const uint64 id = _nextId++;
  _queue.push_back(makeQueuedSong(id, std::move(song), std::move(title),
                                  QueueOrigin::Manual));
  return id;
}

std::optional<uint64> SongQueue::PushNext(string song, string title) {
  return PrioritizeNext(std::move(song), std::move(title)).id;
}

QueueNextOutcome SongQueue::PrioritizeNext(string song, string title) {
  std::lock_guard lock(_queueMtx);
  if (song.empty())
    return {QueueNextResult::Invalid, std::nullopt};
  const auto matches = [&](const auto &queued) {
    return queued.query == song ||
           (!queued.source.empty() && queued.source == song);
  };
  if (!_queue.empty() && matches(_queue.front())) {
    if (!title.empty())
      _queue.front().title = std::move(title);
    return {QueueNextResult::AlreadyNext, _queue.front().id};
  }
  const auto duplicate = std::find_if(_queue.begin(), _queue.end(), matches);
  if (duplicate != _queue.end()) {
    QueuedSong queued = std::move(*duplicate);
    if (!title.empty())
      queued.title = std::move(title);
    queued.origin = QueueOrigin::Manual;
    _queue.erase(duplicate);
    const auto id = queued.id;
    _queue.push_front(std::move(queued));
    return {QueueNextResult::Moved, id};
  }
  if (_queue.size() >= MAX_SONGS)
    return {QueueNextResult::Full, std::nullopt};
  const uint64 id = _nextId++;
  _queue.push_front(makeQueuedSong(id, std::move(song), std::move(title),
                                   QueueOrigin::Manual));
  return {QueueNextResult::Added, id};
}

bool SongQueue::PushPlaylistCycle(vector<TrackRequest> songs) {
  std::lock_guard lock(_queueMtx);
  if (songs.empty() || !_queue.empty() || songs.size() > MAX_SONGS ||
      std::any_of(songs.begin(), songs.end(),
                  [](const auto &song) { return song.source.empty(); })) {
    return false;
  }
  for (auto &song : songs) {
    const uint64 id = _nextId++;
    _queue.push_back(makeQueuedSong(id, std::move(song.source),
                                    std::move(song.title),
                                    QueueOrigin::Playlist));
  }
  return true;
}

bool SongQueue::AppendPlaylist(TrackRequest song, bool loopContinuation) {
  std::lock_guard lock(_queueMtx);
  if (song.source.empty() || _queue.size() >= MAX_SONGS)
    return false;
  const uint64 id = _nextId++;
  _queue.push_back(makeQueuedSong(id, std::move(song.source),
                                  std::move(song.title),
                                  loopContinuation ? QueueOrigin::PlaylistLoop
                                                   : QueueOrigin::Playlist));
  return true;
}

std::optional<QueuedSong> SongQueue::DropLastPlaylist() {
  std::lock_guard lock(_queueMtx);
  const auto reverse = std::find_if(
      _queue.rbegin(), _queue.rend(),
      [](const auto &song) { return song.origin != QueueOrigin::Manual; });
  if (reverse == _queue.rend())
    return std::nullopt;
  const auto item = std::prev(reverse.base());
  QueuedSong removed = std::move(*item);
  _queue.erase(item);
  return removed;
}

vector<QueuedSong> SongQueue::RemoveLoopContinuations() {
  std::lock_guard lock(_queueMtx);
  vector<QueuedSong> removed;
  for (auto item = _queue.begin(); item != _queue.end();) {
    if (item->origin == QueueOrigin::PlaylistLoop) {
      removed.push_back(std::move(*item));
      item = _queue.erase(item);
    } else {
      ++item;
    }
  }
  return removed;
}

std::optional<QueuedSong> SongQueue::Remove(uint64 id) {
  std::lock_guard lock(_queueMtx);
  const auto item =
      std::find_if(_queue.begin(), _queue.end(),
                   [id](const auto &song) { return song.id == id; });
  if (item == _queue.end())
    return std::nullopt;
  QueuedSong removed = std::move(*item);
  _queue.erase(item);
  return removed;
}

std::optional<QueuedSong> SongQueue::PromoteToFront(uint64 id) {
  std::lock_guard lock(_queueMtx);
  const auto item =
      std::find_if(_queue.begin(), _queue.end(),
                   [id](const auto &song) { return song.id == id; });
  if (item == _queue.end())
    return std::nullopt;
  if (item != _queue.begin()) {
    QueuedSong promoted = std::move(*item);
    _queue.erase(item);
    _queue.push_front(std::move(promoted));
  }
  _queue.front().origin = QueueOrigin::Manual;
  return _queue.front();
}

QueueMoveResult SongQueue::Move(uint64 id, int direction) {
  std::lock_guard lock(_queueMtx);
  const auto item =
      std::find_if(_queue.begin(), _queue.end(),
                   [id](const auto &song) { return song.id == id; });
  if (item == _queue.end())
    return QueueMoveResult::NotFound;
  if (direction == 0)
    return QueueMoveResult::Boundary;

  auto target = item;
  if (direction < 0) {
    if (item == _queue.begin())
      return QueueMoveResult::Boundary;
    --target;
  } else {
    ++target;
    if (target == _queue.end())
      return QueueMoveResult::Boundary;
  }

  const bool itemIsManual = item->origin == QueueOrigin::Manual;
  const bool targetIsManual = target->origin == QueueOrigin::Manual;
  if (itemIsManual != targetIsManual)
    return QueueMoveResult::PriorityBoundary;
  std::iter_swap(item, target);
  return QueueMoveResult::Moved;
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

bool SongQueue::MarkReady(uint64 id, string path, string source, string title,
                          bool temporary,
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
  if (!title.empty())
    song->title = std::move(title);
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
