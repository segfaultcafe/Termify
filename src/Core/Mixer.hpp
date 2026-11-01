#ifndef _TERMIFY_CORE_MIXER_HPP
#define _TERMIFY_CORE_MIXER_HPP

#include "Common.hpp"
#include "FFmpegDecoder.hpp"

#include <deque>

namespace termify::core {

class Mixer {
public:
  Mixer(PlaybackContext *playback, RingBufferContext *ringBuffer,
        VisualizationContext *visualization, MixerResponseContext *responses,
        SongQueue *queue, LocalStore *store = nullptr);
  ~Mixer();

  Mixer(const Mixer &) = delete;
  Mixer &operator=(const Mixer &) = delete;

  bool AddToQueue(string song, string title = {});
  bool PlayNow(string song, string title = {});
  bool StartPlaylist(vector<TrackRequest> songs, string name, bool shuffle,
                     bool loop, std::int64_t playlistId = 0);
  bool SetPlaylistLoop(std::int64_t playlistId, bool enabled);
  void ToggleRepeat(bool enabled);
  void Pause();
  void Resume();
  void NextSong();
  void Stop(bool force = false, bool clearQueue = true);
  void ClearQueue();
  bool PlayQueuedNow(uint64 id);
  bool RemoveQueued(uint64 id);
  bool MoveQueued(uint64 id, int direction);
  void SetVolume(int percent);
  bool SeekToSeconds(std::int64_t seconds, bool announce = true);
  bool SeekRelativeSeconds(std::int64_t seconds, bool announce = true);
  bool SeekToFraction(double fraction, bool announce = true);
  void Shutdown();
  void PublishMessage(const string &message, bool isError = false);

  string TrackName() const;
  string CurrentSource() const;
  vector<QueuedSong> UpcomingTracks(std::size_t limit = 3);
  int Volume() const { return _ctx.playbackCtx->volumePercent.load(); }
  bool IsRepeatOn() const { return _repeatSong.load(); }
  bool IsPlaylistLooping() const;
  AtomicContext *GetCtxRef() { return &_ctx; }

private:
  void schedulerLoop();
  void prefetchLoop();
  void startNext();
  bool queuePlaylistCycle();
  void replenishPlaylistQueue(std::size_t minimum = 3);
  void deferPlaylistTrack(TrackRequest track, bool loopContinuation);
  bool prioritizeNext(string song, string title, bool playNow);
  bool requestSeek(uint64 targetFrames, bool announce);
  void clearQueue(bool cancelPlaylist);
  static void cleanupPrepared(const QueuedSong &song);
  void dispatchResponse(const string &message, bool isError = false);

  AtomicContext _ctx{};
  atomic_bool _repeatSong{false};
  mutable std::mutex _trackMtx;
  string _trackName;
  string _trackSource;
  mutable std::mutex _playlistMtx;
  vector<TrackRequest> _playlistSongs;
  vector<TrackRequest> _playlistAppendOrder;
  std::size_t _playlistAppendIndex{0};
  struct DeferredPlaylistTrack {
    TrackRequest track;
    bool loopContinuation{false};
  };
  std::deque<DeferredPlaylistTrack> _playlistDeferred;
  string _playlistName;
  std::int64_t _playlistId{0};
  bool _playlistShuffle{false};
  bool _playlistLoop{false};
  bool _playlistActive{false};
  bool _playlistStarting{false};
  std::mutex _schedulerMtx;
  condition_variable _schedulerCv;
  std::mutex _prefetchMtx;
  condition_variable _prefetchCv;
  atomic_bool _prefetchCancel{false};
  std::mutex _controlMtx;
  std::thread _schedulerThread;
  std::thread _prefetchThread;
};

} // namespace termify::core

#endif // _TERMIFY_CORE_MIXER_HPP
