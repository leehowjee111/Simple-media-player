#include "../header/demuxer.h"
#include <cstdint>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>

Demuxer::~Demuxer() { uninit(); }
bool Demuxer::init(int argc, const char *path) {
  if (argc < 2) {
    return false;
  }
  if (avformat_open_input(&fmt_ctx_, path, nullptr, nullptr) < 0) {
    return false;
  }
  if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0) {
    return false;
  }
  video_stream_index_ =
      av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  audio_stream_index_ =
      av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

  win_width_ = fmt_ctx_->streams[video_stream_index_]->codecpar->width;
  win_height_ = fmt_ctx_->streams[video_stream_index_]->codecpar->height;
  return true;
}

int Demuxer::read_packet(AVPacket *pkt) {
  if (!fmt_ctx_ || pkt == nullptr) {
    return AVERROR(EINVAL);
  }
  int ret = av_read_frame(fmt_ctx_, pkt);
  if (ret < 0) {
    return ret;
  }
  while (true) {
    if (pkt->stream_index == video_stream_index_ ||
        pkt->stream_index == audio_stream_index_) {
      break;
    }
    av_packet_unref(pkt);
    ret = av_read_frame(fmt_ctx_, pkt);
    if (ret < 0) {
      break;
    }
  }
  return ret;
}
bool Demuxer::seek(double seconds) {
  if (!fmt_ctx_) {
    return false;
  }
  // Timestamp in AV_TIME_BASE units (microseconds) for stream_index=-1.
  int64_t ts = static_cast<int64_t>(seconds * AV_TIME_BASE);
  std::cout << "[DemuxerThread]: ts: " << ts << " (target: " << seconds
            << "s)\n";

  // Seek to the keyframe at or before the target timestamp.
  // We'll discard leading audio samples before the target in swr_and_push().
  int ret = avformat_seek_file(fmt_ctx_, -1, INT64_MIN, ts, INT64_MAX,
                               AVSEEK_FLAG_BACKWARD);
  if (ret < 0) {
    ret = av_seek_frame(fmt_ctx_, -1, ts, AVSEEK_FLAG_BACKWARD);
  }
  std::cout << "[DemuxerThread]: demuxer seek done, ret=" << ret << "\n";
  return ret >= 0;
}
double Demuxer::get_fps() const {
  auto stream = fmt_ctx_->streams[video_stream_index_];
  auto fr = av_guess_frame_rate(fmt_ctx_, stream, nullptr);
  auto fps = av_q2d(fr);
  return fps;
}

void Demuxer::uninit() { avformat_close_input(&fmt_ctx_); }

AVCodecParameters *Demuxer::get_video_codec_parameters() const {
  if (video_stream_index_ >= 0) {
    return fmt_ctx_->streams[video_stream_index_]->codecpar;
  }
  return nullptr;
}
AVCodecParameters *Demuxer::get_audio_codec_parameters() const {
  if (audio_stream_index_ >= 0) {
    return fmt_ctx_->streams[audio_stream_index_]->codecpar;
  }
  return nullptr;
}
