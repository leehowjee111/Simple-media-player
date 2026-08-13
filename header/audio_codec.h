#pragma once
#include "SDL_audio.h"
#include "codec.h"
#include <SDL2/SDL.h>
extern "C" {
#include <libswresample/swresample.h>
}
class AudioCodec : public Codec {
public:
  ~AudioCodec();
  bool init(AVCodecParameters *) override;
  bool init(SDL_AudioSpec &);
  int send_packet(AVPacket *) override;
  int decode(AVFrame *) override;
  void flush() override;
  void clean() override;

  int get_sample_rate() const { return codec_ctx_->sample_rate; }
  int get_channels() const { return codec_ctx_->ch_layout.nb_channels; }
  SwrContext *get_swr_ctx() const { return swr_ctx; }

private:
  SwrContext *swr_ctx = nullptr;
};
