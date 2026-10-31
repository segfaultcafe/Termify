#ifndef _TERMIFY_CORE_MIXER_HPP
#define _TERMIFY_CORE_MIXER_HPP

#include "Common.hpp"
#include "FFmpegDecoder.hpp"

namespace termify::core {

class Mixer {
public:
  Mixer(PlaybackContext *playback, RingBufferContext *ringBuffer,
        VisualizationContext *visualization, MixerResponseContext *responses,
        SongQueue *queue, LocalStore *store = nullptr);
  ~Mixer();

  Mixer(const Mixer &) = delete;
  Mixer &operator=(const Mixer &) = delete;

  bool AddToQueue(string song);
  bool StartPlaylist(vector<string> songs, string name, bool shuffle,
                     bool loop);
  void ToggleRepeat(bool enabled);
  void Pause();
  void Resume();
  void NextSong();
  void Stop(bool force = false, bool clearQueue = true);
  void ClearQueue();
  void SetVolume(int percent);
  void Shutdown();
  void PublishMessage(const string &message, bool isError = false);

  string TrackName() const;
  string CurrentSource() const;
  int Volume() const { return _ctx.playbackCtx->volumePercent.load(); }
  bool IsRepeatOn() const { return _repeatSong.load(); }
  AtomicContext *GetCtxRef() { return &_ctx; }

private:
  void schedulerLoop();
  void prefetchLoop();
  void startNext();
  bool queuePlaylistCycle();
  bool enqueue(string song, bool next = false);
  void clearQueue(bool cancelPlaylist);
  static void cleanupPrepared(const QueuedSong &song);
  void dispatchResponse(const string &message, bool isError = false);

  AtomicContext _ctx{};
  atomic_bool _repeatSong{false};
  mutable std::mutex _trackMtx;
  string _trackName;
  string _trackSource;
  std::mutex _playlistMtx;
  vector<string> _playlistSongs;
  string _playlistName;
  bool _playlistShuffle{false};
  bool _playlistLoop{false};
  bool _playlistActive{false};
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
