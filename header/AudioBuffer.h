#pragma once
#include <cstdint>
#include <mutex>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

class AudioBuffer {
public:
  AudioBuffer() = default;
  ~AudioBuffer() = default;
  void push(std::vector<uint8_t>);
  void read(uint8_t *stream, int len);
  int total_samples_read();

private:
  std::vector<uint8_t> cache_;
  int used_sample = 0;
  std::mutex mtx_;
};
