#include "../header/player.h"
#include "SDL_audio.h"
#include "SDL_keycode.h"
#include "SDL_timer.h"
#include <chrono>
#include <libavcodec/codec_id.h>
#include <mutex>
#include <thread>

Player::~Player() { uninit(); }
bool Player::init(int argc, const char *path) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
    return false;
  }
  if (!demuxer_.init(argc, path)) {
    return false;
  }
  width_ = demuxer_.get_win_width();
  height_ = demuxer_.get_win_height();
  window_ = SDL_CreateWindow("MyPlay", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, width_, height_ + 15, 0);

  if (!window_) {
    return false;
  }

  renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);

  if (!renderer_) {
    return false;
  }

  texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_IYUV,
                               SDL_TEXTUREACCESS_STREAMING, width_, height_);
  if (!texture_) {
    return false;
  }

  if (!vc_.init(demuxer_.get_video_codec_parameters())) {
    return false;
  }

  if (!ac_.init(demuxer_.get_audio_codec_parameters())) {
    return false;
  }

  wanted.freq = ac_.get_sample_rate();
  wanted.format = AUDIO_S16SYS;
  wanted.channels = ac_.get_channels();
  wanted.samples = 1024;
  wanted.callback = audio_callback;
  wanted.userdata = this;
  audio_dev = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
  if (audio_dev == 0) {
    return false;
  }

  ab_.set_sample_rate(obtained.freq);
  ab_.set_channels(obtained.channels);
  ac_.init(obtained);

  video_frame_duration = 1000 / demuxer_.get_fps();
  // std::cout << demuxer_.get_fps() << "\n";
  // std::cout << ac_.get_channels() << "\n";
  video_frame_ = av_frame_alloc();
  audio_frame_ = av_frame_alloc();
  // decode_thread = std::thread(&Player::demuxer_loop, this);
  // if(ab_.get_cache_size() < 33)

  get_packet_t_ = std::thread(&Player::demuxer_thread, this);
  video_decode_t_ = std::thread(&Player::video_decode, this);
  audio_decode_t_ = std::thread(&Player::audio_decode, this);

  SDL_PauseAudioDevice(audio_dev, 0);

  return true;
}
void Player::audio_callback(void *userdata, uint8_t *stream, int len) {
  Player *player = static_cast<Player *>(userdata);
  memset(stream, 0, len);
  player->ab_.read(stream, len);
}
void Player::start() { render(); }

void Player::swr_and_push() {
  out_data[0] = nullptr;
  int out_samples =
      av_rescale_rnd(swr_get_delay(ac_.get_swr_ctx(), ac_.get_sample_rate()) +
                         audio_frame_->nb_samples,
                     obtained.freq, audio_frame_->sample_rate, AV_ROUND_UP);
  av_samples_alloc(out_data, nullptr, obtained.channels, out_samples,
                   AV_SAMPLE_FMT_S16, 0);
  int nums = swr_convert(ac_.get_swr_ctx(), out_data, out_samples,
                         (const uint8_t **)audio_frame_->data,
                         audio_frame_->nb_samples);
  int bytes =
      nums * obtained.channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

  if (!first_audio_frame_) {
    double frame_pts_sec = 0;
    if (audio_frame_->pts != AV_NOPTS_VALUE) {
      frame_pts_sec =
          audio_frame_->pts * av_q2d(demuxer_.get_audio_time_base());
    } else if (seek_target_ > 0) {
      // No PTS available — assume we're at the seek target to avoid
      // trimming valid audio.
      frame_pts_sec = seek_target_;
    }

    // If we seeked to a target ahead of this keyframe, compute how many
    // leading audio bytes to discard so playback starts at the target.
    if (seek_target_ > 0 && frame_pts_sec < seek_target_) {
      double skip_sec = seek_target_ - frame_pts_sec;
      audio_skip_bytes_ =
          static_cast<int64_t>(skip_sec * obtained.freq * obtained.channels *
                               av_get_bytes_per_sample(AV_SAMPLE_FMT_S16));
      ab_.reset_base_pts(seek_target_);
    } else {
      ab_.reset_base_pts(frame_pts_sec);
    }
    first_audio_frame_ = true;
  }

  // Discard leading audio bytes that belong to the interval [keyframe, target).
  if (audio_skip_bytes_ > 0) {
    if (audio_skip_bytes_ >= bytes) {
      audio_skip_bytes_ -= bytes;
      av_freep(&out_data[0]);
      return;
    }
    int skip = static_cast<int>(audio_skip_bytes_);
    audio_skip_bytes_ = 0;
    ab_.push(std::vector<uint8_t>(out_data[0] + skip, out_data[0] + bytes));
    av_freep(&out_data[0]);
    return;
  }

  ab_.push(std::vector<uint8_t>(out_data[0], out_data[0] + bytes));
  av_freep(&out_data[0]);
}

void Player::audio_decode() {
  AVPacket *pkt = nullptr;
  while (true) {
    pkt = ap_.pop();
    if (pkt == nullptr) {
      break;
    }

    // Capture generation so we can detect if a seek happened while decoding.
    int64_t gen = seek_generation_.load();

    int ret = ac_.send_packet(pkt);
    if (ret >= 0) {
      while (true) {
        ret = ac_.decode(audio_frame_);
        if (ret < 0 || ret == AVERROR(EINVAL) || ret == AVERROR(EAGAIN) ||
            ret == AVERROR_EOF) {
          break;
        }
        // Discard this frame if a seek happened after we started processing
        // this packet — it belongs to the old playback position.
        if (gen != seek_generation_.load()) {
          av_frame_unref(audio_frame_);
          continue;
        }
        swr_and_push();
      }
    }
    av_packet_free(&pkt);
  }

  ac_.send_packet(nullptr);
  while (true) {
    int ret = ac_.decode(audio_frame_);
    if (ret < 0 || ret == AVERROR(EINVAL) || ret == AVERROR(EAGAIN) ||
        ret == AVERROR_EOF) {
      break;
    }
    swr_and_push();
  }
  ac_.flush();
}
void Player::video_decode() {
  AVPacket *pkt = nullptr;
  while (true) {
    pkt = fp_.pop();
    if (pkt == nullptr) {
      break;
    }

    // Capture generation so we can detect if a seek happened while decoding.
    int64_t gen = seek_generation_.load();

    int ret = vc_.send_packet(pkt);
    if (ret >= 0) {
      while (true) {
        ret = vc_.decode(video_frame_);
        if (ret < 0 || ret == AVERROR(EINVAL) || ret == AVERROR(EAGAIN) ||
            ret == AVERROR_EOF) {
          break;
        }
        // Discard this frame if a seek happened after we started processing
        // this packet — it belongs to the old playback position.
        if (gen != seek_generation_.load()) {
          av_frame_unref(video_frame_);
          continue;
        }
        fq_.push(video_frame_);
      }
    }
    av_packet_free(&pkt);
  }
  vc_.flush();
  fq_.stop();
}
void Player::render() {
  while (running_ && !first_audio_frame_) {
    SDL_Delay(10);
  }
  while (running_ && ab_.get_cache_size() < 32768) {
    SDL_Delay(5);
  }
  while (running_) {
    while (SDL_PollEvent(&event_)) {
      switch (event_.type) {
      case SDL_QUIT:
        running_ = false;
        std::cout << "do it \n";
        break;
      case SDL_KEYDOWN: {
        if (event_.key.keysym.sym == SDLK_ESCAPE) {
          running_ = false;
          std::cout << "do it \n";
        }
        if (event_.key.keysym.sym == SDLK_SPACE) {
          if (paused) {
            unpause();
          } else {
            pause();
          }
        }
        if (event_.key.keysym.sym == SDLK_LEFT) {
          seek(audio_clock - 1.0);
        }
        if (event_.key.keysym.sym == SDLK_RIGHT) {
          seek(audio_clock + 1.0);
          std::cout << "[RenderThread]: right seek\n";
        }
        break;
      }
      case SDL_MOUSEBUTTONDOWN: {

        if (event_.button.button == SDL_BUTTON_LEFT) {
          if (event_.button.y >= height_) {
            double ratio = event_.button.x * 1.0 / width_;
            double pos = ratio * demuxer_.get_duration();
            seek(pos);
          }
        }
        break;
      }
      default:
        break;
      }
    }

    if (paused) {
      continue;
    }
    auto frame = fq_.pop_timeout(10);
    // std::cout << fq_.get_size() << "\n";
    if (!frame) {
      SDL_Delay(10);
      continue;
    }
    std::cout << "[RenderThread]: after seek\n";
    video_clock = frame->pts * av_q2d(demuxer_.get_video_time_base());
    audio_clock = ab_.get_audio_clock();
    std::cout << "[RenderThread]: audio_clock: " << audio_clock
              << "\t video_clock: " << video_clock << "\n";
    double diff = audio_clock - video_clock;
    // std::cout << ab_.get_cache_size() << "\t" << fq_.get_size() << "\t"
    //           << audio_clock << "\t" << video_clock << "\t" << diff << "\t"
    //           << video_frame_duration << "\n";
    Uint32 wait = 0;
    if (diff > 0.05) {
      av_frame_free(&frame);
      continue;
    } else if (diff < -0.05) {
      wait = static_cast<Uint32>(-diff * 800);
    }
    // std::cout << ap_.size() << "\t" << fp_.size() << "\n";
    SDL_Delay(video_frame_duration + wait);
    SDL_UpdateYUVTexture(texture_, nullptr, frame->data[0], frame->linesize[0],
                         frame->data[1], frame->linesize[1], frame->data[2],
                         frame->linesize[2]);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_SetRenderDrawColor(renderer_, 80, 80, 80, 255);
    SDL_Rect back_rect{0, height_, width_, 15};
    SDL_RenderFillRect(renderer_, &back_rect);
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    double duration = demuxer_.get_duration();
    SDL_Rect front_rect{
        0, height_, static_cast<int>((audio_clock / duration) * width_), 15};
    SDL_RenderFillRect(renderer_, &front_rect);
    SDL_Rect video_rect{0, 0, width_, height_};
    SDL_RenderCopy(renderer_, texture_, nullptr, &video_rect);
    SDL_RenderPresent(renderer_);
    av_frame_free(&frame);
  }
}

void Player::seek(double pos) {
  if (pos < 0) {
    pos = 0;
  }
  double duration = demuxer_.get_duration();
  std::cout << "[RenderThread]: duration: " << duration << "\n";
  if (duration > 0 && pos >= duration)
    pos = duration;
  std::unique_lock<std::mutex> locker(seek_mtx_);
  std::cout << "[RenderThread]: seek locker\n";
  seek_pos_ = pos;
  seek_done_ = false;
  seek_cond_.wait(locker, [this]() { return seek_done_; });
  std::cout << "[RenderThread]: seek wait\n";
}

void Player::do_seek(double pos) {
  std::cout << "[DemuxerThread]: pos: " << pos << "\n";
  std::cout << "[DemuxerThread]: do seek\n";

  // Bump generation so decode threads can discard in-flight stale frames.
  seek_generation_.fetch_add(1);

  // Seek FIRST — if it fails, we don't touch any buffers and playback
  // continues.
  if (!demuxer_.seek(pos)) {
    std::cout << "[DemuxerThread]: seek failed, aborting\n";
    return;
  }
  std::cout << "[DemuxerThread]: seek done\n";

  // Seek succeeded — now reset the pipeline.
  SDL_PauseAudioDevice(audio_dev, 1);
  fp_.flush();
  ap_.flush();
  fq_.clear();
  ab_.clear();
  ab_.reset_base_pts(pos);
  seek_target_ = pos; // remember target for audio trimming in swr_and_push
  vc_.flush_buffers();
  ac_.flush_buffers();

  // Second clear: catch any stale frames pushed between the first clear
  // and the codec flush by decode threads still processing old packets.
  fq_.clear();
  ab_.clear();

  std::cout << "[DemuxerThread]: clear done\n";
  first_audio_frame_ = false;
  audio_skip_bytes_ = 0;

  SDL_PauseAudioDevice(audio_dev, 0);
}

void Player::demuxer_thread() {
  AVPacket *pkt = av_packet_alloc();
  while (running_) {
    std::cout << "[DemuxerThread]: demuxer_thread begin\n";
    {
      std::lock_guard<std::mutex> locker(seek_mtx_);
      if (seek_pos_ >= 0) {
        do_seek(seek_pos_);
        std::cout << "[DemuxerThread]: d_seek\n";
        seek_pos_ = -1;
        seek_done_ = true;
        seek_cond_.notify_one();
        continue;
      }
    }
    std::cout << "[DemuxerThread]: read_packet\n";
    int ret = demuxer_.read_packet(pkt);
    if (ret == AVERROR(EINVAL)) {
      break;
    }
    if (ret < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    auto clone = av_packet_alloc();
    av_packet_ref(clone, pkt);
    if (pkt->stream_index == demuxer_.get_audio_stream_index()) {
      std::cout << "[DemuxerThread]: audio_stream packet\n";
      ap_.push(clone);
    } else if (pkt->stream_index == demuxer_.get_video_stream_index()) {
      std::cout << "[DemuxerThread]: video_stream packet\n";
      fp_.push(clone);
    } else {
      av_packet_free(&clone);
    }
    av_packet_unref(pkt);
  }
  av_packet_free(&pkt);
  // 文件播放到结尾(EOF):唤醒音视频解码线程,让它们 flush 解码器;
  // video_decode 结束时会给 fq_ 发 stop,渲染线程才能从 fq_.pop() 中返回,
  // 继续轮询 SDL 事件——否则按下 ESC/关闭窗口会毫无反应。
  ap_.stop();
  fp_.stop();
  std::cout << "[DemuxerThread]: demuxer_thread done\n";
}

void Player::pause() {
  // 暂停音频-> SDL_PauseAudioDevice()
  // 暂停视频-> 持续continue;
  paused = true;
  SDL_PauseAudioDevice(audio_dev, 1);
}

void Player::unpause() {
  paused = false;
  SDL_PauseAudioDevice(audio_dev, 0);
}

void Player::uninit() {
  running_ = false;
  fp_.stop();
  ap_.stop();
  fq_.stop();
  if (get_packet_t_.joinable()) {
    get_packet_t_.join();
  }
  if (video_decode_t_.joinable()) {
    video_decode_t_.join();
  }
  if (audio_decode_t_.joinable()) {
    audio_decode_t_.join();
  }
  // if (decode_thread.joinable()) {
  //   decode_thread.join();
  // }
  if (audio_frame_) {
    av_frame_free(&audio_frame_);
  }
  if (video_frame_) {
    av_frame_free(&video_frame_);
  }
  if (texture_) {
    SDL_DestroyTexture(texture_);
  }
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
  }
  if (window_) {
    SDL_DestroyWindow(window_);
  }
  if (audio_dev) {
    SDL_PauseAudioDevice(audio_dev, 1);
    SDL_CloseAudioDevice(audio_dev);
  }
  SDL_Quit();
}
