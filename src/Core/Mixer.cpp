#include "Mixer.hpp"
#include "AudioDownloader.hpp"

#include <random>

namespace termify::core {

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
  _schedulerThread = std::thread([this] { schedulerLoop(); });
  _prefetchThread = std::thread([this] { prefetchLoop(); });
}

Mixer::~Mixer() { Shutdown(); }

bool Mixer::AddToQueue(string song) {
  std::error_code pathError;
  if (std::filesystem::is_regular_file(song, pathError))
    song = std::filesystem::absolute(song).lexically_normal().string();
  return prioritizeNext(std::move(song), false);
}

bool Mixer::PlayNow(string song) {
  std::error_code pathError;
  if (std::filesystem::is_regular_file(song, pathError))
    song = std::filesystem::absolute(song).lexically_normal().string();
  return prioritizeNext(std::move(song), true);
}

bool Mixer::prioritizeNext(string song, bool playNow) {
  const string displayName = song;
  const auto outcome = _ctx.trackQueue->PrioritizeNext(std::move(song));
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

bool Mixer::StartPlaylist(vector<string> songs, string name, bool shuffle,
                          bool loop) {
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
    _playlistShuffle = shuffle;
    _playlistLoop = loop;
    _playlistActive = true;
  }
  if (!queuePlaylistCycle())
    return false;
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
    bool loopPlaylist = false;
    {
      std::lock_guard playlistLock(_playlistMtx);
      loopPlaylist = _playlistActive && _playlistLoop;
    }
    if (loopPlaylist)
      queuePlaylistCycle();
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
    _playlistSongs.clear();
  }
  for (const auto &song : _ctx.trackQueue->Clear()) {
    cleanupPrepared(song);
  }
  _schedulerCv.notify_one();
  _prefetchCv.notify_one();
}

bool Mixer::queuePlaylistCycle() {
  vector<string> songs;
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
  _schedulerCv.notify_one();
  _prefetchCv.notify_one();
  return true;
}

void Mixer::SetVolume(int percent) {
  const int clamped = std::clamp(percent, 0, 100);
  _ctx.playbackCtx->volumePercent.store(clamped);
  dispatchResponse("Volume set to " + std::to_string(clamped) + "%.");
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
                                      prepared.temporary,
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
  _ctx.source = song.query;
  {
    std::lock_guard trackLock(_trackMtx);
    _trackName = song.query;
    _trackSource = song.source.empty() ? song.query : song.source;
  }
  {
    std::lock_guard metadataLock(_ctx.playbackCtx->metadataMtx);
    _ctx.playbackCtx->metadata = {song.query, {}, {}};
  }

  _ctx.playbackCtx->shouldStop.store(false);
  _ctx.playbackCtx->isPaused.store(false);
  _ctx.playbackCtx->isLoading.store(true);
  _ctx.playbackCtx->isPlaying.store(true);
  _ctx.playbackCtx->positionFrames.store(0);
  _ctx.playbackCtx->durationFrames.store(0);
  dispatchResponse("Now playing: " + song.query);
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
        preparedSong.temporary = prepared.temporary;
        preparedSong.cacheLease = std::move(prepared.cacheLease);
      }
      {
        std::lock_guard trackLock(_trackMtx);
        _trackSource = preparedSong.source.empty() ? preparedSong.query
                                                   : preparedSong.source;
        _ctx.source = _trackSource;
      }
      do {
        FFmpegDecoder decoder(_ctx);
        decoder.Stream(preparedSong.preparedPath, preparedSong.query);
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
    _ctx.ringBufferCtx->ringCv.notify_all();
    _schedulerCv.notify_one();

    if (!failed && !_ctx.playbackCtx->shouldStop.load() &&
        _ctx.trackQueue->Empty()) {
      bool loop = false;
      {
        std::lock_guard playlistLock(_playlistMtx);
        loop = _playlistActive && _playlistLoop;
        if (_playlistActive && !loop)
          _playlistActive = false;
      }
      if (loop && queuePlaylistCycle())
        dispatchResponse("Looping playlist: " + _playlistName);
      else
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
