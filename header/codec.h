#pragma once
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

class Codec {
public:
  virtual bool init(AVCodecParameters *) = 0;
  virtual int send_packet(AVPacket *) = 0;
  virtual int decode(AVFrame *) = 0;
  virtual void flush() = 0;
  virtual void clean() = 0;
  void flush_buffers() {
    if (codec_ctx_)
      avcodec_flush_buffers(codec_ctx_);
  }

protected:
  AVCodecContext *codec_ctx_ = nullptr;
};
