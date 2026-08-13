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
  size_t max_size_ = 1024;
  bool stopped_ = false;

public:
  FrameQueue(size_t max_size = 10240) : max_size_(max_size){};
  ~FrameQueue();
  void push(AVFrame *frame);
  AVFrame *pop();
  AVFrame *pop_timeout(int ms); // 超时返回 nullptr,供渲染线程定期醒来轮询事件
  void stop();
  void clear();

  size_t get_size() const { return queue_.size(); }
  size_t max_size() const { return max_size_; }
};
