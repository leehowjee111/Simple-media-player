#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

class AudioBuffer {
  static constexpr size_t MAX_AUDIO_SIZES_ = 88200;

public:
  AudioBuffer() = default;
  ~AudioBuffer() = default;
  void push(std::vector<uint8_t>);
  void read(uint8_t *stream, int len);
  void clear();

  double get_audio_clock();
  void set_sample_rate(int rate);
  void set_channels(int channels);
  void reset_base_pts(double pts);
  size_t get_cache_size() const { return cache_.size(); }

private:
  std::deque<uint8_t> cache_;
  int sample_rate = 44100;
  double base_pts = 0;
  int played_sample = 0;
  int channels_ = 2;
  std::mutex mtx_;
};
