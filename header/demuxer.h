#pragma once
#include <iostream>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}
class Demuxer {
public:
  ~Demuxer();
  bool init(int argc, const char *path);
  int read_packet(AVPacket *);
  bool seek(double);
  void uninit();

  int get_win_width() const { return win_width_; }
  int get_win_height() const { return win_height_; }

  int get_video_stream_index() const { return video_stream_index_; }
  int get_audio_stream_index() const { return audio_stream_index_; }

  AVCodecParameters *get_video_codec_parameters() const;
  AVCodecParameters *get_audio_codec_parameters() const;

  AVRational get_video_time_base() const {
    return fmt_ctx_->streams[video_stream_index_]->time_base;
  }
  AVRational get_audio_time_base() const {
    return fmt_ctx_->streams[audio_stream_index_]->time_base;
  }

  double get_fps() const;
  double get_duration() const {
    return fmt_ctx_->duration / (double)AV_TIME_BASE;
  }

private:
  AVFormatContext *fmt_ctx_ = nullptr;
  int video_stream_index_ = -1;
  int audio_stream_index_ = -1;
  int win_width_ = 1920;
  int win_height_ = 1080;
};
