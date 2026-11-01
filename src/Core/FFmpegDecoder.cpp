#include "FFmpegDecoder.hpp"
#include "RingBuffer.hpp"
#include <libavcodec/avcodec.h>

namespace termify::core {

FFmpegDecoder::FFmpegDecoder(AtomicContext &ctx) : _ctx(ctx) {}

void FFmpegDecoder::Stream(const string &source, const string &displayName) {
  if (_ctx.playbackCtx->shouldStop.load())
    return;

  _fmtCtx = nullptr;
  _swr = nullptr;
  _ctx.ringBufferCtx->decodeFinished.store(false);
  _ctx.ringBufferCtx->writeIndex.store(0);
  _ctx.ringBufferCtx->readIndex.store(0);
  _ctx.playbackCtx->positionFrames.store(0);
  _ctx.playbackCtx->historyRecorded.store(false);

  AVCodecContext *codecCtx = nullptr;
  snd_pcm_t *pcm = nullptr;
  AVPacket *packet = nullptr;
  AVFrame *frame = nullptr;
  uint8_t *outputBuffer = nullptr;
  std::thread consumer;
  std::atomic_bool outputFinished{false};

  auto cleanup = [&] {
    if (consumer.joinable()) {
      consumer.join();
    }
    delete[] outputBuffer;
    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&_swr);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&_fmtCtx);
    if (pcm) {
      snd_pcm_drop(pcm);
      snd_pcm_close(pcm);
      pcm = nullptr;
    }
  };

  try {
    if (source.empty()) {
      throw std::runtime_error("No playable source was produced");
    }

    const int streamIndex = init(source, &codecCtx);
    publishStreamInfo(displayName, streamIndex);
    _ctx.playbackCtx->isLoading.store(false);
    resampleStereo(codecCtx);
    openAlsa(&pcm);

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    constexpr uint64 BUFFER_CAPACITY = 192000;
    outputBuffer = new uint8_t[BUFFER_CAPACITY];
    if (!packet || !frame) {
      throw std::runtime_error("FFmpeg buffer allocation failed");
    }

    consumer = std::thread([&] {
      worker(&pcm);
      outputFinished.store(true);
      _ctx.ringBufferCtx->ringCv.notify_all();
    });
    decodeResample(codecCtx, packet, frame, streamIndex, BUFFER_CAPACITY,
                   outputBuffer, &outputFinished);
    cleanup();
  } catch (...) {
    _ctx.playbackCtx->isLoading.store(false);
    const bool cancellationRequested =
        _ctx.playbackCtx->shouldStop.load();
    _ctx.playbackCtx->shouldStop.store(true);
    _ctx.ringBufferCtx->decodeFinished.store(true);
    _ctx.ringBufferCtx->ringCv.notify_all();
    _ctx.playbackCtx->pauseCv.notify_all();
    cleanup();
    if (!cancellationRequested) {
      _ctx.playbackCtx->shouldStop.store(false);
    }
    throw;
  }
}

int FFmpegDecoder::init(const string &source, AVCodecContext **ppCodecCtx) {
  avformat_network_init();
  av_log_set_level(AV_LOG_QUIET);
  _fmtCtx = nullptr;
  if (avformat_open_input(&_fmtCtx, source.c_str(), nullptr, nullptr) < 0)
    throw std::runtime_error("Failed to open input");
  if (avformat_find_stream_info(_fmtCtx, nullptr) < 0)
    throw std::runtime_error("Failed to read stream info");

  int streamIndex =
      av_find_best_stream(_fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (streamIndex < 0)
    throw std::runtime_error("No audio stream");

  AVCodecParameters *codecpar = _fmtCtx->streams[streamIndex]->codecpar;
  const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
  if (!codec)
    throw std::runtime_error("No compatible audio decoder");
  *ppCodecCtx = avcodec_alloc_context3(codec);
  if (!*ppCodecCtx ||
      avcodec_parameters_to_context(*ppCodecCtx, codecpar) < 0 ||
      avcodec_open2(*ppCodecCtx, codec, nullptr) < 0)
    throw std::runtime_error("Failed to initialize the audio decoder");
  return streamIndex;
}

void FFmpegDecoder::publishStreamInfo(const string &displayName,
                                      int streamIndex) {
  uint64 durationFrames = 0;
  if (_fmtCtx->duration != AV_NOPTS_VALUE && _fmtCtx->duration > 0) {
    durationFrames = static_cast<uint64>(_fmtCtx->duration) * 44100 /
                     static_cast<uint64>(AV_TIME_BASE);
  } else {
    const AVStream *stream = _fmtCtx->streams[streamIndex];
    if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
      const double seconds = stream->duration * av_q2d(stream->time_base);
      durationFrames = static_cast<uint64>(std::max(0.0, seconds) * 44100.0);
    }
  }
  _ctx.playbackCtx->durationFrames.store(durationFrames);

  auto metadataValue = [this, streamIndex](const char *key) -> string {
    AVDictionaryEntry *entry =
        av_dict_get(_fmtCtx->metadata, key, nullptr, AV_DICT_IGNORE_SUFFIX);
    if (!entry) {
      entry = av_dict_get(_fmtCtx->streams[streamIndex]->metadata, key, nullptr,
                          AV_DICT_IGNORE_SUFFIX);
    }
    return entry && entry->value ? entry->value : "";
  };

  TrackMetadata metadata;
  metadata.title = metadataValue("title");
  metadata.artist = metadataValue("artist");
  metadata.album = metadataValue("album");
  if (metadata.title.empty()) {
    metadata.title = displayName;
  }
  std::lock_guard lock(_ctx.playbackCtx->metadataMtx);
  _ctx.playbackCtx->metadata = std::move(metadata);
}

void FFmpegDecoder::resampleStereo(AVCodecContext *codecCtx) {
  _swr = swr_alloc();
  if (!_swr)
    throw std::runtime_error("Failed to allocate the audio resampler");
  AVChannelLayout stereo;
  av_channel_layout_default(&stereo, _ctx.ringBufferCtx->CHANNELS);
  av_opt_set_chlayout(_swr, "in_chlayout", &codecCtx->ch_layout, 0);
  av_opt_set_sample_fmt(_swr, "in_sample_fmt", codecCtx->sample_fmt, 0);
  av_opt_set_int(_swr, "in_sample_rate", codecCtx->sample_rate, 0);
  av_opt_set_chlayout(_swr, "out_chlayout", &stereo, 0);
  av_opt_set_sample_fmt(_swr, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
  av_opt_set_int(_swr, "out_sample_rate", 44100, 0);
  if (swr_init(_swr) < 0)
    throw std::runtime_error("Failed to initialize the audio resampler");
}

void FFmpegDecoder::openAlsa(snd_pcm_t **ppPcm) {
  int result = snd_pcm_open(ppPcm, "pipewire", SND_PCM_STREAM_PLAYBACK, 0);
  if (result < 0)
    result = snd_pcm_open(ppPcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
  if (result < 0)
    throw std::runtime_error("No ALSA playback device is available");
  if (snd_pcm_set_params(*ppPcm, SND_PCM_FORMAT_S16_LE,
                         SND_PCM_ACCESS_RW_INTERLEAVED,
                         _ctx.ringBufferCtx->CHANNELS, 44100, 1, 500000) < 0)
    throw std::runtime_error("ALSA device configuration failed");
}

void FFmpegDecoder::decodeResample(AVCodecContext *codecCtx, AVPacket *pkt,
                                   AVFrame *frame, int streamIndex,
                                   uint64 bufCapacity, uint8_t *outBuf,
                                   const std::atomic_bool *outputFinished) {
  while (!_ctx.playbackCtx->shouldStop.load()) {
    if (applyPendingSeek(codecCtx, streamIndex, pkt, frame))
      continue;
    if (av_read_frame(_fmtCtx, pkt) < 0) {
      _ctx.ringBufferCtx->decodeFinished.store(true);
      _ctx.ringBufferCtx->ringCv.notify_all();
      std::unique_lock lock(_ctx.ringBufferCtx->ringMtx);
      _ctx.ringBufferCtx->ringCv.wait(lock, [&] {
        return _ctx.playbackCtx->shouldStop.load() ||
               _ctx.playbackCtx->seekTargetFrames.load() >= 0 ||
               outputFinished->load();
      });
      if (outputFinished->load() || _ctx.playbackCtx->shouldStop.load())
        break;
      _ctx.ringBufferCtx->decodeFinished.store(false);
      continue;
    }
    bool seekPending = false;
    if (pkt->stream_index == streamIndex) {
      avcodec_send_packet(codecCtx, pkt);
      while (avcodec_receive_frame(codecCtx, frame) == 0 &&
             !_ctx.playbackCtx->shouldStop.load()) {
        if (_ctx.playbackCtx->seekTargetFrames.load() >= 0) {
          seekPending = true;
          break;
        }
        int outSamples = swr_convert(
            _swr, &outBuf,
            bufCapacity / sizeof(int16_t) / _ctx.ringBufferCtx->CHANNELS,
            (const uint8_t **)frame->data, frame->nb_samples);
        size_t samples = outSamples * _ctx.ringBufferCtx->CHANNELS, done = 0;
        while (done < samples && !_ctx.playbackCtx->shouldStop.load()) {
          std::unique_lock lk(_ctx.ringBufferCtx->ringMtx);
          size_t ri = _ctx.ringBufferCtx->readIndex,
                 wi = _ctx.ringBufferCtx->writeIndex,
                 rb = _ctx.ringBufferCtx->ringBuffer.size();
          size_t freeSp = (ri + rb - wi - 1) % rb;
          _ctx.ringBufferCtx->ringCv.wait(lk, [&]() {
            size_t ri2 = _ctx.ringBufferCtx->readIndex,
                   wi2 = _ctx.ringBufferCtx->writeIndex;
            return _ctx.playbackCtx->shouldStop.load() ||
                   _ctx.playbackCtx->seekTargetFrames.load() >= 0 ||
                   ((ri2 + rb - wi2 - 1) % rb) > 0;
          });

          if (_ctx.playbackCtx->seekTargetFrames.load() >= 0) {
            seekPending = true;
            break;
          }

          ri = _ctx.ringBufferCtx->readIndex;
          wi = _ctx.ringBufferCtx->writeIndex;
          freeSp = (ri + rb - wi - 1) % rb;
          size_t cont = std::min({freeSp, samples - done, rb - wi});
          memcpy(&_ctx.ringBufferCtx->ringBuffer[wi], ((int16 *)outBuf) + done,
                 cont * sizeof(int16));
          _ctx.ringBufferCtx->writeIndex.store((wi + cont) % rb);
          done += cont;
          lk.unlock();
          _ctx.ringBufferCtx->ringCv.notify_all();
        }
        if (seekPending)
          break;
      }
    }
    av_packet_unref(pkt);
    if (seekPending)
      continue;
  }

  // signal end
  _ctx.ringBufferCtx->decodeFinished.store(true);
  _ctx.ringBufferCtx->ringCv.notify_all();
}

bool FFmpegDecoder::applyPendingSeek(AVCodecContext *codecCtx, int streamIndex,
                                     AVPacket *packet, AVFrame *frame) {
  const auto target = _ctx.playbackCtx->seekTargetFrames.exchange(-1);
  if (target < 0)
    return false;
  _ctx.ringBufferCtx->decodeFinished.store(false);
  const AVRational frames{1, 44100};
  const auto timestamp = av_rescale_q(target, frames,
                                      _fmtCtx->streams[streamIndex]->time_base);
  if (av_seek_frame(_fmtCtx, streamIndex, timestamp, AVSEEK_FLAG_BACKWARD) <
      0) {
    _ctx.ringBufferCtx->decodeFinished.store(true);
    _ctx.ringBufferCtx->ringCv.notify_all();
    return false;
  }

  avcodec_flush_buffers(codecCtx);
  swr_close(_swr);
  if (swr_init(_swr) < 0)
    throw std::runtime_error("Failed to reset audio after seeking");
  av_packet_unref(packet);
  av_frame_unref(frame);
  {
    std::lock_guard lock(_ctx.ringBufferCtx->ringMtx);
    _ctx.ringBufferCtx->readIndex.store(0);
    _ctx.ringBufferCtx->writeIndex.store(0);
  }
  _ctx.playbackCtx->positionFrames.store(static_cast<uint64>(target));
  _ctx.playbackCtx->seekGeneration.fetch_add(1);
  _ctx.ringBufferCtx->ringCv.notify_all();
  _ctx.playbackCtx->pauseCv.notify_all();
  return true;
}

void FFmpegDecoder::worker(snd_pcm_t **ppPcm) { RingBuffer::Update(_ctx, ppPcm); }

} // namespace termify::core
