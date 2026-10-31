#include "RingBuffer.hpp"
#include "Common.hpp"
#include <algorithm>
#include <mutex>
#include <ctime>

namespace termify::core {

void RingBuffer::Update(AtomicContext &ctx, snd_pcm_t **ppPcm) {
  auto &ringBufferCtx = ctx.ringBufferCtx;
  auto &playbackCtx = ctx.playbackCtx;
  auto &visualCtx = ctx.visualizationCtx;
  while (!ringBufferCtx->decodeFinished.load() ||
         ringBufferCtx->readIndex.load() != ringBufferCtx->writeIndex.load()) {
    if (playbackCtx->shouldStop.load())
      break;
    std::unique_lock lk(ringBufferCtx->ringMtx);
    ringBufferCtx->ringCv.wait(lk, [&]() {
      return playbackCtx->shouldStop.load() ||
             ringBufferCtx->decodeFinished.load() ||
             ringBufferCtx->writeIndex.load() !=
                 ringBufferCtx->readIndex.load();
    });
    if (playbackCtx->shouldStop.load())
      break;
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

    while (playbackCtx->isPaused.load()) {
      std::unique_lock plk(playbackCtx->pauseMtx);
      playbackCtx->pauseCv.wait(plk, [&]() {
        return !playbackCtx->isPaused.load() || playbackCtx->shouldStop.load();
      });

      if (playbackCtx->shouldStop.load()) {
        return;
      }
    }

    // write to ALSA (retry on XRUN)
    int frames = toRead / ringBufferCtx->CHANNELS, off = 0;
    while (frames > 0) {
      int w = snd_pcm_writei(*ppPcm, chunk.data() + off * ringBufferCtx->CHANNELS,
                             frames);
      if (w == -EPIPE) {
        snd_pcm_prepare(*ppPcm);
        continue;
      }
      if (w < 0) {
        snd_pcm_prepare(*ppPcm);
        break;
      }
      frames -= w;
      off += w;
      const auto previous = playbackCtx->positionFrames.fetch_add(static_cast<uint64>(w));
      if (w > 0 && previous == 0 && ctx.store) {
        HistoryEntry entry;
        entry.source = ctx.source;
        entry.durationSeconds = static_cast<std::int64_t>(playbackCtx->durationFrames.load() / 44100);
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
  }
}

} // namespace termify::core
