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
    memcpy(stream, cache_.data(), read_len);
    cache_.erase(cache_.begin(), cache_.begin() + read_len);
  }
  used_sample += read_len / 2;
}
int AudioBuffer::total_samples_read() {
  std::lock_guard<std::mutex> locker(mtx_);
  return used_sample;
}
