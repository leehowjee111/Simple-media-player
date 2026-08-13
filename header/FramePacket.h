#include <condition_variable>
#include <mutex>
extern "C" {
#include <libavcodec/avcodec.h>
}
#include <queue>
class FramePacket {
public:
  FramePacket(size_t max_size) : max_size_(max_size){};
  ~FramePacket();
  void push(AVPacket *);
  AVPacket *pop();
  void flush();
  void stop();
  void clean();
  size_t size() const { return q_.size(); }
  // void seek_stop();
  // void seek_done();

private:
  std::queue<AVPacket *> q_;
  std::mutex mtx_;
  std::condition_variable pro_cond_, con_cond_;
  bool seek = false;
  bool stop_ = false;
  size_t max_size_ = 0;
};
