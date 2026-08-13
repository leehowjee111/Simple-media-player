#include "../header/AudioBuffer.h"
#include <mutex>

void AudioBuffer::push(std::vector<uint8_t> data) {
  std::lock_guard<std::mutex> locker(mtx_);
  cache_.insert(cache_.end(), data.begin(), data.end());
}

void AudioBuffer::read(uint8_t *stream, int len) {
  std::lock_guard<std::mutex> locker(mtx_);
  size_t read_len = std::min(static_cast<int>(cache_.size()), len);
  if (read_len > 0) {
    auto it = cache_.begin();
    for (size_t i = 0; i < read_len; ++i, ++it) {
      stream[i] = *it;
    }
    cache_.erase(cache_.begin(), cache_.begin() + read_len);
  }
  played_sample += read_len / (2 * channels_);
}
void AudioBuffer::set_sample_rate(int rate) {
  std::lock_guard<std::mutex> locker(mtx_);
  sample_rate = rate;
}
void AudioBuffer::set_channels(int channels) {
  std::lock_guard<std::mutex> locker(mtx_);
  channels_ = channels;
}
void AudioBuffer::reset_base_pts(double pts) {
  std::lock_guard<std::mutex> locker(mtx_);
  base_pts = pts;
  played_sample = 0;
}
double AudioBuffer::get_audio_clock() {
  std::lock_guard<std::mutex> locker(mtx_);
  return base_pts + (double)played_sample / sample_rate;
}
void AudioBuffer::clear() {
  std::lock_guard<std::mutex> locker(mtx_);
  cache_.clear();
}
