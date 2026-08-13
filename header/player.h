#pragma once
#include "AudioBuffer.h"
#include "AudioPacket.h"
#include "FramePacket.h"
#include "FrameQueue.h"
#include "SDL_audio.h"
#include "SDL_events.h"
#include "SDL_render.h"
#include "SDL_stdinc.h"
#include "SDL_video.h"
#include "audio_codec.h"
#include "demuxer.h"
#include "video_codec.h"
#include <SDL2/SDL.h>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
class Player {
  static void audio_callback(void *userdata, Uint8 *stream, int len);

public:
  Player(size_t audio_max_size, size_t video_max_size)
      : fp_(video_max_size), ap_(audio_max_size){};
  ~Player();
  bool init(int, const char *);
  void start();
  void uninit();

private:
  // SDL属性
  SDL_Window *window_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  SDL_Event event_;
  SDL_Texture *texture_ = nullptr;
  SDL_AudioDeviceID audio_dev = 0;
  SDL_AudioSpec wanted, obtained;

  std::atomic<bool> running_{true};
  bool first_audio_frame_ = false;
  int64_t audio_skip_bytes_ = 0;            // bytes to skip before seek target
  std::atomic<int64_t> seek_generation_{0}; // incremented on each seek
  int width_ = 1920;
  int height_ = 1080;
  double audio_clock = 0;
  double video_clock = 0;
  double video_frame_duration = 0;

  void render();
  // 音视频处理
  Demuxer demuxer_;
  FrameQueue fq_;
  AudioBuffer ab_;
  FramePacket fp_;
  AudioPacket ap_;
  VideoCodec vc_;
  AudioCodec ac_;
  uint8_t *out_data[1] = {nullptr};
  AVFrame *video_frame_ = nullptr;
  AVFrame *audio_frame_ = nullptr;
  void demuxer_thread();
  void video_decode();
  void audio_decode();
  void swr_and_push();
  std::thread get_packet_t_;
  std::thread video_decode_t_;
  std::thread audio_decode_t_;

  // 其他功能
  bool paused = false;
  bool seek_done_ = true;
  std::mutex seek_mtx_;
  std::condition_variable seek_cond_;
  double seek_pos_ = -1;
  double seek_target_ = -1; // desired position for audio sample trimming
  void pause();
  void unpause();
  void do_seek(double pos);
  void seek(double pos);
};
