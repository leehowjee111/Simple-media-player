#include <condition_variable>
#include <mutex>
extern "C" {
#include <libavcodec/avcodec.h>
}
#include <queue>
class AudioPacket {
public:
  AudioPacket(size_t max_size) : max_size_(max_size){};
  ~AudioPacket();
  void push(AVPacket *);
  AVPacket *pop();
  void flush();
  void stop();
  void clean();
  // void seek_stop();
  // void seek_done();
  size_t size() const { return q_.size(); }

private:
  std::queue<AVPacket *> q_;
  std::mutex mtx_;
  std::condition_variable pro_cond_, con_cond_;
  bool seek = false;
  bool stop_ = false;
  size_t max_size_ = 0;
};
