#include "Mixer.hpp"
#include "AudioDownloader.hpp"

#include <iomanip>
#include <random>

namespace termify::core {

namespace {
string formatSeekPosition(uint64 frames) {
  const uint64 seconds = frames / 44100;
  std::ostringstream output;
  output << seconds / 60 << ':' << std::setw(2) << std::setfill('0')
         << seconds % 60;
  return output.str();
}
} // namespace

Mixer::Mixer(PlaybackContext *playback, RingBufferContext *ringBuffer,
             VisualizationContext *visualization,
             MixerResponseContext *responses, SongQueue *queue,
             LocalStore *store) {
  _ctx.playbackCtx = playback;
  _ctx.ringBufferCtx = ringBuffer;
  _ctx.visualizationCtx = visualization;
  _ctx.mResponseCtx = responses;
  _ctx.trackQueue = queue;
  _ctx.store = store;
  if (store) {
    try {
      if (const auto saved = store->SavedVolume())
        _ctx.playbackCtx->volumePercent.store(*saved);
    } catch (const std::exception &) {
      // Playback remains available when a setting cannot be read.
    }
  }
  _schedulerThread = std::thread([this] { schedulerLoop(); });
  _prefetchThread = std::thread([this] { prefetchLoop(); });
}

Mixer::~Mixer() { Shutdown(); }

bool Mixer::AddToQueue(string song, string title) {
  std::error_code pathError;
  if (std::filesystem::is_regular_file(song, pathError))
    song = std::filesystem::absolute(song).lexically_normal().string();
  return prioritizeNext(std::move(song), std::move(title), false);
}

bool Mixer::PlayNow(string song, string title) {
  std::error_code pathError;
  if (std::filesystem::is_regular_file(song, pathError))
    song = std::filesystem::absolute(song).lexically_normal().string();
  return prioritizeNext(std::move(song), std::move(title), true);
}

bool Mixer::prioritizeNext(string song, string title, bool playNow) {
  const string displayName = title.empty() ? song : title;
  auto outcome = _ctx.trackQueue->PrioritizeNext(song, title);
  if (outcome.result == QueueNextResult::Full) {
    if (auto displaced = _ctx.trackQueue->DropLastPlaylist()) {
      deferPlaylistTrack(
          {displaced->query, displaced->title},
          displaced->origin == QueueOrigin::PlaylistLoop);
      cleanupPrepared(*displaced);
      outcome =
          _ctx.trackQueue->PrioritizeNext(std::move(song), std::move(title));
    }
  }
  if (outcome.result == QueueNextResult::Invalid) {
    dispatchResponse("Choose a valid track first.", true);
    return false;
  }
  if (outcome.result == QueueNextResult::Full) {
    dispatchResponse("The queue is full (20 tracks).", true);
    return false;
  }
  if (playNow) {
    Stop(true, false);
    dispatchResponse("Playing now: " + displayName);
  } else {
    _schedulerCv.notify_one();
    if (outcome.result == QueueNextResult::AlreadyNext) {
      dispatchResponse("Already next: " + displayName);
    } else if (outcome.result == QueueNextResult::Moved) {
      dispatchResponse("Moved to next: " + displayName);
    } else {
      dispatchResponse("Playing next: " + displayName);
    }
  }
  _prefetchCv.notify_one();
  return true;
}

bool Mixer::StartPlaylist(vector<TrackRequest> songs, string name, bool shuffle,
                          bool loop, std::int64_t playlistId) {
  if (songs.empty()) {
    dispatchResponse("This playlist has no tracks.", true);
    return false;
  }
  if (songs.size() > SongQueue::MAX_SONGS) {
    dispatchResponse("A playlist can contain at most 20 tracks.", true);
    return false;
  }
  _repeatSong.store(false);
  Stop(true, true);
  {
    std::lock_guard playlistLock(_playlistMtx);
    _playlistSongs = std::move(songs);
    _playlistName = std::move(name);
    _playlistId = playlistId;
    _playlistShuffle = shuffle;
    _playlistLoop = loop;
    _playlistActive = true;
    _playlistStarting = true;
    _playlistAppendOrder.clear();
    _playlistAppendIndex = 0;
    _playlistDeferred.clear();
  }
  if (!queuePlaylistCycle()) {
    std::lock_guard playlistLock(_playlistMtx);
    _playlistStarting = false;
    _playlistActive = false;
    return false;
  }
  {
    std::lock_guard playlistLock(_playlistMtx);
    _playlistStarting = false;
  }
  dispatchResponse("Playing playlist: " + _playlistName +
                   (shuffle ? " · shuffle" : "") + (loop ? " · loop" : ""));
  return true;
}

string Mixer::TrackName() const {
  std::lock_guard lock(_trackMtx);
  return _trackName;
}

string Mixer::CurrentSource() const {
  std::lock_guard lock(_trackMtx);
  return _trackSource;
}

vector<QueuedSong> Mixer::UpcomingTracks(std::size_t limit) {
  replenishPlaylistQueue(limit);
  return _ctx.trackQueue->Snapshot(limit);
}

bool Mixer::IsPlaylistLooping() const {
  std::lock_guard playlistLock(_playlistMtx);
  return _playlistActive && _playlistLoop;
}

bool Mixer::SetPlaylistLoop(std::int64_t playlistId, bool enabled) {
  {
    std::lock_guard playlistLock(_playlistMtx);
    if (!_playlistActive || _playlistId != playlistId)
      return false;
    _playlistLoop = enabled;
    if (!enabled) {
      _playlistAppendOrder.clear();
      _playlistAppendIndex = 0;
      std::erase_if(_playlistDeferred, [](const auto &track) {
        return track.loopContinuation;
      });
    }
  }
  if (!enabled) {
    for (const auto &song : _ctx.trackQueue->RemoveLoopContinuations())
      cleanupPrepared(song);
  } else {
    replenishPlaylistQueue();
  }
  _schedulerCv.notify_one();
  _prefetchCv.notify_one();
  return true;
}

void Mixer::Pause() {
  if (!_ctx.playbackCtx->isPlaying.load() ||
      _ctx.playbackCtx->isPaused.load()) {
    dispatchResponse("Nothing is currently playing.", true);
    return;
  }

  _ctx.playbackCtx->isPaused.store(true);
  dispatchResponse("Playback paused.");
}

void Mixer::Resume() {
  if (!_ctx.playbackCtx->isPlaying.load() ||
      !_ctx.playbackCtx->isPaused.load()) {
    dispatchResponse("Nothing is paused.", true);
    return;
  }

  _ctx.playbackCtx->isPaused.store(false);
  _ctx.playbackCtx->pauseCv.notify_all();
  _ctx.ringBufferCtx->ringCv.notify_all();
  dispatchResponse("Playback resumed.");
}

void Mixer::NextSong() {
  if (_ctx.trackQueue->Empty()) {
    replenishPlaylistQueue(1);
  }
  if (_ctx.trackQueue->Empty()) {
    dispatchResponse("There is no next track in the queue.", true);
    return;
  }

  dispatchResponse("Skipping to the next track...");
  Stop(true, false);
}

void Mixer::Stop(bool force, bool clearQueue) {
  const bool hadQueuedTracks = clearQueue && !_ctx.trackQueue->Empty();
  if (clearQueue) {
    ClearQueue();
  }

  std::lock_guard controlLock(_controlMtx);
  const bool active = _ctx.playbackCtx->isPlaying.load() ||
                      _ctx.playbackCtx->isPaused.load() ||
                      _ctx.playbackCtx->playThread.joinable();
  if (!active) {
    if (!force) {
      if (hadQueuedTracks) {
        dispatchResponse("Queue cleared.");
      } else {
        dispatchResponse("Nothing is currently playing.", true);
      }
    }
    // PlayNow queues its target before stopping the current track. When the
    // mixer was already idle, this is the notification that starts it.
    _schedulerCv.notify_one();
    return;
  }

  _ctx.playbackCtx->shouldStop.store(true);
  _ctx.playbackCtx->isPaused.store(false);
  _ctx.ringBufferCtx->ringCv.notify_all();
  _ctx.playbackCtx->pauseCv.notify_all();

  if (_ctx.playbackCtx->playThread.joinable()) {
    _ctx.playbackCtx->playThread.join();
  }

  _ctx.playbackCtx->isPlaying.store(false);
  _ctx.playbackCtx->isLoading.store(false);
  _ctx.playbackCtx->shouldStop.store(false);
  _ctx.playbackCtx->positionFrames.store(0);
  _ctx.playbackCtx->durationFrames.store(0);
  _ctx.playbackCtx->seekTargetFrames.store(-1);
  {
    std::lock_guard trackLock(_trackMtx);
    _trackName.clear();
    _trackSource.clear();
  }
  {
    std::lock_guard metadataLock(_ctx.playbackCtx->metadataMtx);
    _ctx.playbackCtx->metadata = {};
  }

  if (clearQueue && !force) {
    dispatchResponse("Playback stopped and the queue was cleared.");
  }

  _schedulerCv.notify_one();
  _ctx.ringBufferCtx->ringCv.notify_all();
}

void Mixer::ClearQueue() { clearQueue(true); }

void Mixer::clearQueue(bool cancelPlaylist) {
  if (cancelPlaylist) {
    std::lock_guard playlistLock(_playlistMtx);
    _playlistActive = false;
    _playlistStarting = false;
    _playlistId = 0;
    _playlistSongs.clear();
    _playlistAppendOrder.clear();
    _playlistAppendIndex = 0;
    _playlistDeferred.clear();
  }
  for (const auto &song : _ctx.trackQueue->Clear()) {
    cleanupPrepared(song);
  }
  _schedulerCv.notify_one();
  _prefetchCv.notify_one();
}

bool Mixer::queuePlaylistCycle() {
  vector<TrackRequest> songs;
  bool shuffle = false;
  {
    std::lock_guard playlistLock(_playlistMtx);
    if (!_playlistActive || _playlistSongs.empty())
      return false;
    songs = _playlistSongs;
    shuffle = _playlistShuffle;
  }
  if (shuffle) {
    std::random_device device;
    std::mt19937 generator(device());
    std::shuffle(songs.begin(), songs.end(), generator);
  }
  if (!_ctx.trackQueue->PushPlaylistCycle(std::move(songs))) {
    return false;
  }
  {
    std::lock_guard playlistLock(_playlistMtx);
    _playlistAppendOrder.clear();
    _playlistAppendIndex = 0;
  }
  _schedulerCv.notify_one();
  _prefetchCv.notify_one();
  return true;
}

void Mixer::deferPlaylistTrack(TrackRequest track, bool loopContinuation) {
  std::lock_guard playlistLock(_playlistMtx);
  if (_playlistActive)
    _playlistDeferred.push_front(
        {std::move(track), loopContinuation});
}

void Mixer::replenishPlaylistQueue(std::size_t minimum) {
  minimum = std::min(minimum, SongQueue::MAX_SONGS);
  bool added = false;
  while (_ctx.trackQueue->Size() < minimum) {
    std::lock_guard playlistLock(_playlistMtx);
    if (!_playlistActive || _playlistStarting)
      break;
    std::optional<TrackRequest> next;
    bool loopContinuation = false;
    if (!_playlistDeferred.empty()) {
      next = std::move(_playlistDeferred.front().track);
      loopContinuation = _playlistDeferred.front().loopContinuation;
      _playlistDeferred.pop_front();
    } else {
      if (!_playlistLoop || _playlistSongs.empty())
        break;
      if (_playlistAppendIndex >= _playlistAppendOrder.size()) {
        _playlistAppendOrder = _playlistSongs;
        _playlistAppendIndex = 0;
        if (_playlistShuffle) {
          std::random_device device;
          std::mt19937 generator(device());
          std::shuffle(_playlistAppendOrder.begin(),
                       _playlistAppendOrder.end(), generator);
        }
      }
      next = _playlistAppendOrder[_playlistAppendIndex++];
      loopContinuation = true;
    }
    if (!_ctx.trackQueue->AppendPlaylist(*next, loopContinuation)) {
      _playlistDeferred.push_front(
          {std::move(*next), loopContinuation});
      break;
    }
    added = true;
  }
  if (added) {
    _schedulerCv.notify_one();
    _prefetchCv.notify_one();
  }
}

void Mixer::SetVolume(int percent) {
  const int clamped = std::clamp(percent, 0, 100);
  _ctx.playbackCtx->volumePercent.store(clamped);
  if (_ctx.store) {
    try {
      _ctx.store->SaveVolume(clamped);
    } catch (const std::exception &error) {
      dispatchResponse(
          "Volume changed for this session but could not be saved: " +
              string(error.what()),
          true);
      return;
    }
  }
  dispatchResponse("Volume set to " + std::to_string(clamped) + "%.");
}

bool Mixer::SeekToSeconds(std::int64_t seconds, bool announce) {
  const uint64 duration = _ctx.playbackCtx->durationFrames.load();
  if (!_ctx.playbackCtx->isPlaying.load() || duration == 0) {
    if (announce)
      dispatchResponse("Nothing seekable is currently playing.", true);
    return false;
  }
  const auto maximum = static_cast<std::int64_t>(duration / 44100);
  const auto clamped = std::clamp<std::int64_t>(seconds, 0, maximum);
  return requestSeek(
      std::min<uint64>(static_cast<uint64>(clamped) * 44100, duration),
      announce);
}

bool Mixer::SeekRelativeSeconds(std::int64_t seconds, bool announce) {
  const uint64 duration = _ctx.playbackCtx->durationFrames.load();
  if (!_ctx.playbackCtx->isPlaying.load() || duration == 0) {
    if (announce)
      dispatchResponse("Nothing seekable is currently playing.", true);
    return false;
  }
  const auto pending = _ctx.playbackCtx->seekTargetFrames.load();
  const uint64 currentFrames = pending >= 0
                                   ? static_cast<uint64>(pending)
                                   : _ctx.playbackCtx->positionFrames.load();
  const long double current =
      static_cast<long double>(currentFrames) / 44100.0L;
  const long double maximum = static_cast<long double>(duration / 44100);
  const auto target = static_cast<uint64>(std::clamp(
      current + static_cast<long double>(seconds), 0.0L, maximum));
  return requestSeek(std::min<uint64>(target * 44100, duration), announce);
}

bool Mixer::SeekToFraction(double fraction, bool announce) {
  const uint64 duration = _ctx.playbackCtx->durationFrames.load();
  if (!_ctx.playbackCtx->isPlaying.load() || duration == 0) {
    if (announce)
      dispatchResponse("Nothing seekable is currently playing.", true);
    return false;
  }
  const auto target = static_cast<uint64>(
      std::clamp(fraction, 0.0, 1.0) * static_cast<double>(duration));
  return requestSeek(std::min(target, duration), announce);
}

bool Mixer::requestSeek(uint64 targetFrames, bool announce) {
  const uint64 duration = _ctx.playbackCtx->durationFrames.load();
  targetFrames = std::min(targetFrames, duration);
  _ctx.ringBufferCtx->decodeFinished.store(false);
  _ctx.playbackCtx->seekTargetFrames.store(
      static_cast<std::int64_t>(targetFrames));
  _ctx.ringBufferCtx->ringCv.notify_all();
  _ctx.playbackCtx->pauseCv.notify_all();
  if (announce)
    dispatchResponse("Seeking to " + formatSeekPosition(targetFrames) + ".");
  return true;
}

void Mixer::ToggleRepeat(bool enabled) {
  _repeatSong.store(enabled);
  dispatchResponse(enabled ? "Repeat enabled." : "Repeat disabled.");
}

void Mixer::Shutdown() {
  _ctx.playbackCtx->gRunning.store(false);
  _prefetchCancel.store(true);
  ClearQueue();
  _schedulerCv.notify_all();
  _prefetchCv.notify_all();
  Stop(true, true);

  if (_schedulerThread.joinable() &&
      _schedulerThread.get_id() != std::this_thread::get_id()) {
    _schedulerThread.join();
  }
  if (_prefetchThread.joinable() &&
      _prefetchThread.get_id() != std::this_thread::get_id()) {
    _prefetchThread.join();
  }

  _ctx.playbackCtx->pauseCv.notify_all();
  _ctx.ringBufferCtx->ringCv.notify_all();
}

void Mixer::PublishMessage(const string &message, bool isError) {
  dispatchResponse(message, isError);
}

void Mixer::schedulerLoop() {
  std::unique_lock lock(_schedulerMtx);
  while (true) {
    _schedulerCv.wait(lock, [this] {
      return !_ctx.playbackCtx->gRunning.load() ||
             (!_ctx.playbackCtx->isPlaying.load() &&
              !_ctx.playbackCtx->isPaused.load() &&
              _ctx.trackQueue->ReadyForPlayback());
    });

    if (!_ctx.playbackCtx->gRunning.load()) {
      return;
    }

    lock.unlock();
    startNext();
    lock.lock();
  }
}

void Mixer::prefetchLoop() {
  std::unique_lock lock(_prefetchMtx);
  while (true) {
    _prefetchCv.wait(lock, [this] {
      return !_ctx.playbackCtx->gRunning.load() ||
             (_ctx.playbackCtx->isPlaying.load() &&
              _ctx.trackQueue->HasPending());
    });
    if (!_ctx.playbackCtx->gRunning.load()) {
      return;
    }

    auto song = _ctx.trackQueue->ClaimForPrefetch();
    if (!song) {
      continue;
    }
    lock.unlock();
    try {
      auto prepared = YtDlpManager::Self().PrepareAudio(
          song->query, &_prefetchCancel, _ctx.store);
      if (!_ctx.trackQueue->MarkReady(song->id, prepared.path, prepared.source,
                                      prepared.title, prepared.temporary,
                                      prepared.cacheLease) &&
          prepared.temporary) {
        std::error_code error;
        std::filesystem::remove(prepared.path, error);
      }
    } catch (const std::exception &error) {
      if (_ctx.playbackCtx->gRunning.load()) {
        _ctx.trackQueue->MarkFailed(song->id, error.what());
      }
    }
    _schedulerCv.notify_one();
    lock.lock();
  }
}

void Mixer::startNext() {
  std::lock_guard controlLock(_controlMtx);
  if (!_ctx.playbackCtx->gRunning.load() ||
      _ctx.playbackCtx->isPlaying.load() || _ctx.playbackCtx->isPaused.load()) {
    return;
  }

  auto next = _ctx.trackQueue->PopForPlayback();
  if (!next) {
    return;
  }
  replenishPlaylistQueue();

  if (next->state == QueueState::Failed) {
    dispatchResponse("Could not prepare " + next->query + ": " + next->error,
                     true);
    _schedulerCv.notify_one();
    return;
  }

  if (_ctx.playbackCtx->playThread.joinable()) {
    _ctx.playbackCtx->playThread.join();
  }

  const QueuedSong song = std::move(*next);
  const string displayName = song.title.empty() ? song.query : song.title;
  _ctx.source = song.query;
  {
    std::lock_guard trackLock(_trackMtx);
    _trackName = displayName;
    _trackSource = song.source.empty() ? song.query : song.source;
  }
  {
    std::lock_guard metadataLock(_ctx.playbackCtx->metadataMtx);
    _ctx.playbackCtx->metadata = {displayName, {}, {}};
  }

  _ctx.playbackCtx->shouldStop.store(false);
  _ctx.playbackCtx->isPaused.store(false);
  _ctx.playbackCtx->isLoading.store(true);
  _ctx.playbackCtx->isPlaying.store(true);
  _ctx.playbackCtx->positionFrames.store(0);
  _ctx.playbackCtx->durationFrames.store(0);
  _ctx.playbackCtx->seekTargetFrames.store(-1);
  dispatchResponse("Now playing: " + displayName);
  _prefetchCv.notify_one();

  _ctx.playbackCtx->playThread = std::thread([this, song] {
    bool failed = false;
    QueuedSong preparedSong = song;
    try {
      if (preparedSong.state != QueueState::Ready) {
        auto prepared = YtDlpManager::Self().PrepareAudio(
            preparedSong.query, &_ctx.playbackCtx->shouldStop, _ctx.store);
        preparedSong.preparedPath = std::move(prepared.path);
        preparedSong.source = std::move(prepared.source);
        if (preparedSong.title.empty())
          preparedSong.title = std::move(prepared.title);
        preparedSong.temporary = prepared.temporary;
        preparedSong.cacheLease = std::move(prepared.cacheLease);
      }
      {
        std::lock_guard trackLock(_trackMtx);
        _trackSource = preparedSong.source.empty() ? preparedSong.query
                                                   : preparedSong.source;
        if (!preparedSong.title.empty())
          _trackName = preparedSong.title;
        _ctx.source = _trackSource;
      }
      do {
        FFmpegDecoder decoder(_ctx);
        const string decoderName = preparedSong.title.empty()
                                       ? preparedSong.query
                                       : preparedSong.title;
        decoder.Stream(preparedSong.preparedPath, decoderName);
      } while (_repeatSong.load() && !_ctx.playbackCtx->shouldStop.load());
    } catch (const std::exception &error) {
      failed = !_ctx.playbackCtx->shouldStop.load();
      if (failed) {
        dispatchResponse("Playback failed: " + string(error.what()), true);
      }
    }
    cleanupPrepared(preparedSong);

    {
      std::lock_guard trackLock(_trackMtx);
      _trackName.clear();
      _trackSource.clear();
    }
    {
      std::lock_guard metadataLock(_ctx.playbackCtx->metadataMtx);
      _ctx.playbackCtx->metadata = {};
    }
    _ctx.playbackCtx->isPaused.store(false);
    _ctx.playbackCtx->isLoading.store(false);
    _ctx.playbackCtx->isPlaying.store(false);
    _ctx.playbackCtx->positionFrames.store(0);
    _ctx.playbackCtx->durationFrames.store(0);
    _ctx.playbackCtx->seekTargetFrames.store(-1);
    _ctx.ringBufferCtx->ringCv.notify_all();
    _schedulerCv.notify_one();

    if (!failed && !_ctx.playbackCtx->shouldStop.load()) {
      replenishPlaylistQueue();
    }
    if (!failed && !_ctx.playbackCtx->shouldStop.load() &&
        _ctx.trackQueue->Empty()) {
      {
        std::lock_guard playlistLock(_playlistMtx);
        if (_playlistActive)
          _playlistActive = false;
      }
      dispatchResponse("Queue finished.");
    }
  });
}

void Mixer::cleanupPrepared(const QueuedSong &song) {
  if (!song.temporary || song.preparedPath.empty()) {
    return;
  }
  std::error_code error;
  std::filesystem::remove(song.preparedPath, error);
}

void Mixer::dispatchResponse(const string &message, bool isError) {
  std::lock_guard lock(_ctx.mResponseCtx->resMtx);
  _ctx.mResponseCtx->responses.push({message, isError});
}

} // namespace termify::core
