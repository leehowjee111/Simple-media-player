#include "../header/FrameQueue.h"
#include <chrono>
FrameQueue::~FrameQueue() {
  while (!queue_.empty()) {
    auto frame = queue_.front();
    queue_.pop();
    av_frame_free(&frame);
  }
}
void FrameQueue::push(AVFrame *frame) {
  std::unique_lock<std::mutex> locker(mtx_);
  pro_condition_.wait(
      locker, [this]() { return queue_.size() < max_size_ || stopped_; });
  if (stopped_)
    return;
  AVFrame *c_frame = av_frame_alloc();
  av_frame_move_ref(c_frame, frame);
  queue_.push(c_frame);
  con_condition_.notify_one();
}
AVFrame *FrameQueue::pop() {
  std::unique_lock<std::mutex> locker(mtx_);
  con_condition_.wait(locker, [this]() { return !queue_.empty() || stopped_; });
  if (queue_.empty() && stopped_)
    return nullptr;
  auto frame = std::move(queue_.front());
  queue_.pop();
  pro_condition_.notify_one();
  return frame;
}
AVFrame *FrameQueue::pop_timeout(int ms) {
  std::unique_lock<std::mutex> locker(mtx_);
  con_condition_.wait_for(locker, std::chrono::milliseconds(ms),
                          [this]() { return !queue_.empty() || stopped_; });
  if (queue_.empty()) {
    return nullptr; // 超时(队列暂时为空)或已停止
  }
  auto frame = std::move(queue_.front());
  queue_.pop();
  pro_condition_.notify_one();
  return frame;
}
void FrameQueue::stop() {
  std::lock_guard<std::mutex> locker(mtx_);
  stopped_ = true;
  pro_condition_.notify_all();
  con_condition_.notify_all();
}
void FrameQueue::clear() {
  std::lock_guard<std::mutex> locker(mtx_);
  while (!queue_.empty()) {
    auto frame = queue_.front();
    queue_.pop();
    av_frame_free(&frame);
  }
  pro_condition_.notify_all();
  con_condition_.notify_all();
}
