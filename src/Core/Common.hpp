#ifndef _TERMIFY_CORE_COMMON_HPP
#define _TERMIFY_CORE_COMMON_HPP

#include "Defines.hpp"
#include "SongQueue.hpp"
#include "LocalStore.hpp"
#include <queue>

namespace termify::core {

struct PlaybackContext;
struct RingBufferContext;
struct VisualizationContext;
struct MixerResponse;
struct MixerResponseContext;

const int BUFFER_SIZE = 2048;

struct AtomicContext {
  PlaybackContext *playbackCtx;
  RingBufferContext *ringBufferCtx;
  VisualizationContext *visualizationCtx;
  MixerResponseContext *mResponseCtx;
  SongQueue *trackQueue;
  LocalStore *store{nullptr};
  string source;
};

struct TrackMetadata {
  string title;
  string artist;
  string album;
};

struct PlaybackContext {
  atomic_bool isPlaying{false};
  atomic_bool shouldStop{false};
  atomic_bool gRunning{false};
  atomic_bool isPaused{false};
  atomic_bool isLoading{false};
  atomic_bool historyRecorded{false};
  std::atomic<int> volumePercent{100};
  std::atomic<uint64> positionFrames{0};
  std::atomic<uint64> durationFrames{0};
  std::atomic<std::int64_t> seekTargetFrames{-1};
  std::atomic<uint64> seekGeneration{0};
  std::mutex metadataMtx;
  TrackMetadata metadata;
  std::mutex pauseMtx;
  condition_variable pauseCv;
  std::thread playThread;
};

struct RingBufferContext {
  const int BUFFER_FRAMES = 44100 * 5;
  const int CHANNELS = 1;
  vector<int16> ringBuffer;
  atomic_uint32 writeIndex{0};
  atomic_uint32 readIndex{0};
  std::mutex ringMtx;
  condition_variable ringCv;
  atomic_bool decodeFinished{true};
};

struct VisualizationContext {
  std::array<int16, BUFFER_SIZE> visBuffer{};
  std::mutex visMtx;
  std::mutex coutMtx;
  std::atomic<int> uiLeftColumn{1};
  std::atomic<int> uiTopRow{1};
};

struct MixerResponseContext {
  std::queue<MixerResponse> responses;
  std::mutex resMtx;
};

struct MixerResponse {
  string message;
  bool isErr;
};

} // namespace termify::core

#endif // _TERMIFY_CORE_COMMON_HPP
