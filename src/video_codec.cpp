#include "../header/video_codec.h"
#include <cerrno>
#include <libavcodec/packet.h>
#include <libavutil/error.h>

VideoCodec::~VideoCodec() { clean(); }
bool VideoCodec::init(AVCodecParameters *codec_par) {
  const AVCodec *v_codec = avcodec_find_decoder(codec_par->codec_id);
  codec_ctx_ = avcodec_alloc_context3(v_codec);
  avcodec_parameters_to_context(codec_ctx_, codec_par);
  if (avcodec_open2(codec_ctx_, v_codec, nullptr) < 0) {
    return false;
  }
  return true;
}
int VideoCodec::send_packet(AVPacket *pkt) {
  if (!codec_ctx_ || !pkt) {
    return AVERROR(EINVAL);
  }
  int ret = avcodec_send_packet(codec_ctx_, pkt);
  return ret;
}
int VideoCodec::decode(AVFrame *out_frame) {
  if (!codec_ctx_) {
    return AVERROR(EINVAL);
  }
  int ret = avcodec_receive_frame(codec_ctx_, out_frame);
  return ret;
}

void VideoCodec::flush() {
  if (!codec_ctx_) {
    return;
  }
  avcodec_send_packet(codec_ctx_, nullptr);
  AVFrame *tmp_frame = av_frame_alloc();
  while (avcodec_receive_frame(codec_ctx_, tmp_frame) == 0) {
    av_frame_unref(tmp_frame);
  }
  av_free(tmp_frame);
}

void VideoCodec::clean() {
  if (codec_ctx_) {
    avcodec_free_context(&codec_ctx_);
  }
}
