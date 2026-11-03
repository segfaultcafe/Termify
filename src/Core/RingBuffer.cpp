#include "RingBuffer.hpp"
#include "Common.hpp"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <mutex>

namespace termify::core {

void RingBuffer::Update(AtomicContext &ctx, platform::AudioOutput &output) {
  auto &ringBufferCtx = ctx.ringBufferCtx;
  auto &playbackCtx = ctx.playbackCtx;
  auto &visualCtx = ctx.visualizationCtx;
  uint64 observedSeek = playbackCtx->seekGeneration.load();
  const auto resetDevice = [&] { output.Reset(); };
  while (!ringBufferCtx->decodeFinished.load() ||
         ringBufferCtx->readIndex.load() != ringBufferCtx->writeIndex.load()) {
    if (playbackCtx->shouldStop.load())
      break;
    if (playbackCtx->seekGeneration.load() != observedSeek) {
      observedSeek = playbackCtx->seekGeneration.load();
      resetDevice();
    }
    std::unique_lock lk(ringBufferCtx->ringMtx);
    ringBufferCtx->ringCv.wait(lk, [&]() {
      return playbackCtx->shouldStop.load() ||
             playbackCtx->seekGeneration.load() != observedSeek ||
             ringBufferCtx->decodeFinished.load() ||
             ringBufferCtx->writeIndex.load() !=
                 ringBufferCtx->readIndex.load();
    });
    if (playbackCtx->shouldStop.load())
      break;
    if (playbackCtx->seekGeneration.load() != observedSeek) {
      observedSeek = playbackCtx->seekGeneration.load();
      lk.unlock();
      resetDevice();
      continue;
    }
    uint64 rb = ctx.ringBufferCtx->ringBuffer.size();
    uint64 wi = ctx.ringBufferCtx->writeIndex.load(),
           ri = ctx.ringBufferCtx->readIndex.load();
    uint64 avail = (wi + rb - ri) % rb;
    if (avail == 0 && ctx.ringBufferCtx->decodeFinished)
      break;

    uint64 toRead = std::min<uint64>(avail, BUFFER_SIZE);
    std::vector<int16> chunk(toRead);
    uint32 first = std::min(toRead, rb - ri);
    memcpy(chunk.data(), &ringBufferCtx->ringBuffer[ri],
           first * sizeof(int16_t));
    if (first < toRead)
      memcpy(chunk.data() + first, &ringBufferCtx->ringBuffer[0],
             (toRead - first) * sizeof(int16_t));
    ctx.ringBufferCtx->readIndex.store((ri + toRead) % rb);
    lk.unlock();
    ringBufferCtx->ringCv.notify_all();

    if (playbackCtx->shouldStop.load())
      break;
    if (playbackCtx->seekGeneration.load() != observedSeek) {
      observedSeek = playbackCtx->seekGeneration.load();
      resetDevice();
      continue;
    }

    const int volume = playbackCtx->volumePercent.load();
    if (volume != 100) {
      for (auto &sample : chunk) {
        const int scaled = static_cast<int>(sample) * volume / 100;
        sample = static_cast<int16>(std::clamp(scaled, -32768, 32767));
      }
    }

    // update vis
    {
      std::lock_guard<std::mutex> vl(visualCtx->visMtx);
      uint64 c = std::min(visualCtx->visBuffer.size(), chunk.size());
      memcpy(visualCtx->visBuffer.data(), chunk.data(), c * sizeof(int16_t));
    }

    bool discardChunk = false;
    while (playbackCtx->isPaused.load()) {
      std::unique_lock plk(playbackCtx->pauseMtx);
      playbackCtx->pauseCv.wait(plk, [&]() {
        return !playbackCtx->isPaused.load() ||
               playbackCtx->shouldStop.load() ||
               playbackCtx->seekGeneration.load() != observedSeek;
      });

      if (playbackCtx->shouldStop.load()) {
        return;
      }
      if (playbackCtx->seekGeneration.load() != observedSeek) {
        discardChunk = true;
        break;
      }
    }
    if (discardChunk) {
      observedSeek = playbackCtx->seekGeneration.load();
      resetDevice();
      continue;
    }

    // The platform backend owns device recovery and buffering details.
    int frames = toRead / ringBufferCtx->CHANNELS, off = 0;
    while (frames > 0) {
      if (playbackCtx->seekGeneration.load() != observedSeek) {
        discardChunk = true;
        break;
      }
      const auto written = output.WriteFrames(
          chunk.data() + off * ringBufferCtx->CHANNELS,
          static_cast<std::size_t>(frames));
      if (written == 0) {
        break;
      }
      if (playbackCtx->seekGeneration.load() != observedSeek) {
        discardChunk = true;
        break;
      }
      frames -= static_cast<int>(written);
      off += static_cast<int>(written);
      playbackCtx->positionFrames.fetch_add(static_cast<uint64>(written));
      if (ctx.store &&
          !playbackCtx->historyRecorded.exchange(true)) {
        HistoryEntry entry;
        entry.source = ctx.source;
        entry.durationSeconds = static_cast<std::int64_t>(
            playbackCtx->durationFrames.load() / 44100);
        entry.playedAt = std::time(nullptr);
        {
          std::lock_guard metadataLock(playbackCtx->metadataMtx);
          entry.title = playbackCtx->metadata.title;
          entry.artist = playbackCtx->metadata.artist;
          entry.album = playbackCtx->metadata.album;
        }
        ctx.store->QueueRecord(std::move(entry));
      }
    }
    if (discardChunk) {
      observedSeek = playbackCtx->seekGeneration.load();
      resetDevice();
    }
  }
}

} // namespace termify::core
