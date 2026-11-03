#ifndef _TERMIFY_INCLUDE_FFMPEG_DECODER_HPP
#define _TERMIFY_INCLUDE_FFMPEG_DECODER_HPP

#include "Common.hpp"
#include "Defines.hpp"
#include "../Platform/Services.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace termify::core {

class FFmpegDecoder {
public:
  explicit FFmpegDecoder(AtomicContext &ctx);

  void Stream(const string &source, const string &displayName);

private:
  int init(const string &source, AVCodecContext **ppCodecCtx);
  void publishStreamInfo(const string &displayName, int streamIndex);
  void resampleStereo(AVCodecContext *codecCtx);
  void decodeResample(AVCodecContext *codecCtx, AVPacket *pkt, AVFrame *frame,
                      int streamIndex, uint64 bufCapacity, uint8_t *outBuf,
                      const std::atomic_bool *outputFinished);
  bool applyPendingSeek(AVCodecContext *codecCtx, int streamIndex,
                        AVPacket *packet, AVFrame *frame);

  void worker(platform::AudioOutput &output);

private:
  AtomicContext &_ctx;
  AVFormatContext *_fmtCtx{nullptr};
  SwrContext *_swr{nullptr};
};

} // namespace termify::core

#endif // _TERMIFY_INCLUDE_FFMPEG_DECODER_HPP
