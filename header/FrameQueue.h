#pragma once
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}
class FrameQueue {
private:
  std::queue<AVFrame *> queue_;
  std::mutex mtx_;
  std::condition_variable pro_condition_, con_condition_;
  size_t max_size_;
  bool stopped_ = false;

public:
  FrameQueue(size_t max_size = 10) : max_size_(max_size){};
  ~FrameQueue();
  void push(AVFrame *frame);
  AVFrame *pop();
  void stop();
};
