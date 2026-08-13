#include "../header/audio_codec.h"
#include <libavcodec/avcodec.h>

AudioCodec::~AudioCodec() { clean(); }
bool AudioCodec::init(AVCodecParameters *codec_par) {
  const AVCodec *a_codec = avcodec_find_decoder(codec_par->codec_id);
  codec_ctx_ = avcodec_alloc_context3(a_codec);
  avcodec_parameters_to_context(codec_ctx_, codec_par);
  if (avcodec_open2(codec_ctx_, a_codec, nullptr) < 0) {
    return false;
  }
  return true;
}
bool AudioCodec::init(SDL_AudioSpec &obtained) {
  AVChannelLayout out_layout;
  av_channel_layout_default(&out_layout, obtained.channels);
  swr_alloc_set_opts2(&swr_ctx, &out_layout, AV_SAMPLE_FMT_S16, obtained.freq,
                      &codec_ctx_->ch_layout, codec_ctx_->sample_fmt,
                      codec_ctx_->sample_rate, 0, nullptr);
  swr_init(swr_ctx);
  return true;
}
int AudioCodec::send_packet(AVPacket *pkt) {
  if (!codec_ctx_) {
    return AVERROR(EINVAL);
  }
  int ret = avcodec_send_packet(codec_ctx_, pkt);
  return ret;
}
int AudioCodec::decode(AVFrame *out_frame) {
  if (!codec_ctx_ || !swr_ctx) {
    return AVERROR(EINVAL);
  }
  int ret = avcodec_receive_frame(codec_ctx_, out_frame);
  return ret;
}

void AudioCodec::flush() {
  if (!codec_ctx_) {
    return;
  }
  avcodec_send_packet(codec_ctx_, nullptr);
  AVFrame *tmp_frame = av_frame_alloc();
  while (avcodec_receive_frame(codec_ctx_, tmp_frame) == 0) {
    av_frame_unref(tmp_frame);
  }
  av_frame_free(&tmp_frame);
}
void AudioCodec::clean() {
  if (swr_ctx) {
    swr_free(&swr_ctx);
  }
  if (codec_ctx_) {
    avcodec_free_context(&codec_ctx_);
  }
}
