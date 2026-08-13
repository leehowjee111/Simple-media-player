#include "../header/FramePacket.h"
#include <libavcodec/packet.h>
#include <mutex>

FramePacket::~FramePacket() { clean(); }

void FramePacket::push(AVPacket *pkt) {
  std::unique_lock<std::mutex> locker(mtx_);
  pro_cond_.wait(locker, [this]() { return q_.size() < max_size_ || stop_; });
  if (stop_)
    return;
  q_.push(pkt);
  con_cond_.notify_one();
}
AVPacket *FramePacket::pop() {
  std::unique_lock<std::mutex> locker(mtx_);
  con_cond_.wait(locker, [this]() { return !q_.empty() || stop_; });
  if (stop_ && q_.empty()) {
    return nullptr;
  }
  auto pkt = q_.front();
  q_.pop();
  pro_cond_.notify_one();
  return pkt;
}
void FramePacket::flush() {
  std::lock_guard<std::mutex> locker(mtx_);
  while (!q_.empty()) {
    auto pkt = q_.front();
    q_.pop();
    av_packet_free(&pkt);
  }
  pro_cond_.notify_all();
  con_cond_.notify_all();
}
void FramePacket::stop() {
  std::lock_guard<std::mutex> locker(mtx_);
  stop_ = true;
  pro_cond_.notify_all();
  con_cond_.notify_all();
}

// void FramePacket::seek_stop() {
//   std::lock_guard<std::mutex> locker(mtx_);
//   seek = true;
// }
//
// void FramePacket::seek_done() {
//   std::lock_guard<std::mutex> locker(mtx_);
//   seek = false;
// }

void FramePacket::clean() {
  std::lock_guard<std::mutex> locker(mtx_);
  while (!q_.empty()) {
    auto pkt = q_.front();
    q_.pop();
    av_packet_free(&pkt);
  }
}
