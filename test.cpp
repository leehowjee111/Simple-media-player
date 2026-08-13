#include "SDL_audio.h"
#include "SDL_error.h"
#include "SDL_pixels.h"
#include "SDL_rect.h"
#include "SDL_stdinc.h"
#include "SDL_timer.h"
#include "header/AudioBuffer.h"
#include "header/FrameQueue.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_video.h>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <libavutil/rational.h>
#include <thread>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}
#include <iostream>
#include <random>
#include <string>
using std::cerr;
using std::cout;
std::atomic<bool> seek{false};
std::atomic<int64_t> target_seconds{0};
std::atomic<bool> need_audio_reset_clock{false};
void decode_thread(AVFormatContext *fmt_ctx, SwrContext *swr_ctx,
                   AVCodecContext *video_codec_ctx,
                   AVCodecContext *audio_codec_ctx, int video_stream_index,
                   int audio_stream_index, FrameQueue &frame_q,
                   AudioBuffer &audio_b, SDL_AudioSpec &obtained) {
  AVFrame *frame = av_frame_alloc();
  AVPacket *pkt = av_packet_alloc();
  uint8_t *out_data[1] = {nullptr};
  while (av_read_frame(fmt_ctx, pkt) >= 0) {
    if (seek == true) {
      int64_t time_stamp =
          target_seconds /
          av_q2d(fmt_ctx->streams[video_stream_index]->time_base);
      av_seek_frame(fmt_ctx, video_stream_index, time_stamp,
                    AVSEEK_FLAG_BACKWARD);
      if (audio_codec_ctx) {
        avcodec_flush_buffers(audio_codec_ctx);
      }
      avcodec_flush_buffers(video_codec_ctx);
      audio_b.clear();
      frame_q.clear();
      audio_b.reset_clock(static_cast<int>(target_seconds * obtained.freq));
      need_audio_reset_clock = true;
      seek = false;
      av_packet_unref(pkt);
      continue;
    }
    if (pkt->stream_index == video_stream_index) {
      int ret = avcodec_send_packet(video_codec_ctx, pkt);
      if (ret < 0) {
        cerr << "avcodec_send_packet error\n";
      } else {
        while (true) {
          ret = avcodec_receive_frame(video_codec_ctx, frame);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
          } else if (ret < 0) {
            cerr << "avcodec_receive_frame error\n";
            break;
          }
          frame_q.push(frame);
        }
      }
    }
    if (audio_codec_ctx != nullptr && pkt->stream_index == audio_stream_index) {
      int ret = avcodec_send_packet(audio_codec_ctx, pkt);
      if (ret < 0) {
        cerr << "audio_avcodec_send_packet error\n";
      } else {
        while (true) {
          ret = avcodec_receive_frame(audio_codec_ctx, frame);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
          } else if (ret < 0) {
            cerr << "audio_avcodec_receive_frame error\n";
            break;
          }
          if (need_audio_reset_clock == true) {
            double real_pts =
                frame->pts *
                av_q2d(fmt_ctx->streams[audio_stream_index]->time_base);
            audio_b.reset_clock(static_cast<int>(real_pts * obtained.freq));
            need_audio_reset_clock = false;
          }
          out_data[0] = nullptr;
          int out_samples = av_rescale_rnd(
              swr_get_delay(swr_ctx, audio_codec_ctx->sample_rate) +
                  frame->nb_samples,
              obtained.freq, audio_codec_ctx->sample_rate, AV_ROUND_UP);
          av_samples_alloc(out_data, nullptr, obtained.channels, out_samples,
                           AV_SAMPLE_FMT_S16, 0);
          int nums =
              swr_convert(swr_ctx, out_data, out_samples,
                          (const uint8_t **)frame->data, frame->nb_samples);
          int bytes = nums * obtained.channels *
                      av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
          audio_b.push(std::vector<uint8_t>(out_data[0], out_data[0] + bytes));
          av_freep(&out_data[0]);
        }
      }
    }
    av_packet_unref(pkt);
  };
  avcodec_send_packet(video_codec_ctx, nullptr);
  while (true) {
    int ret = avcodec_receive_frame(video_codec_ctx, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    } else if (ret < 0) {
      cerr << "avcodec_receive_frame error\n";
      break;
    }
    frame_q.push(frame);
  }
  frame_q.stop();
  if (audio_codec_ctx) {
    avcodec_send_packet(audio_codec_ctx, nullptr);
    while (true) {
      int ret = avcodec_receive_frame(audio_codec_ctx, frame);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        break;
      } else if (ret < 0) {
        cerr << "audio_avcodec_receive_frame error\n";
        break;
      }
      if (need_audio_reset_clock == true) {
        double real_pts =
            frame->pts *
            av_q2d(fmt_ctx->streams[audio_stream_index]->time_base);
        audio_b.reset_clock(static_cast<int>(real_pts * obtained.freq));
        need_audio_reset_clock = false;
      }
      out_data[0] = nullptr;
      int out_samples = av_rescale_rnd(
          swr_get_delay(swr_ctx, audio_codec_ctx->sample_rate) +
              frame->nb_samples,
          obtained.freq, audio_codec_ctx->sample_rate, AV_ROUND_UP);
      av_samples_alloc(out_data, nullptr, obtained.channels, out_samples,
                       AV_SAMPLE_FMT_S16, 0);
      int nums = swr_convert(swr_ctx, out_data, out_samples,
                             (const uint8_t **)frame->data, frame->nb_samples);
      int bytes =
          nums * obtained.channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
      audio_b.push(std::vector<uint8_t>(out_data[0], out_data[0] + bytes));
      av_freep(&out_data[0]);
    }
  }
  av_packet_free(&pkt);
  av_frame_free(&frame);
}
void audio_callback(void *userdata, uint8_t *stream, int len) {
  AudioBuffer *buf = static_cast<AudioBuffer *>(userdata);
  memset(stream, 0, len);
  buf->read(stream, len);
}
std::string format_time(int64_t seconds) {
  int min = seconds / 60;
  int sec = seconds % 60;
  char ftime[6];
  snprintf(ftime, 6, "%02d:%02d", min, sec);
  return std::string(ftime);
}
int main(int argc, char *argv[]) {
  /*--------------------音视频资源解码部分------------------------*/
  if (argc < 2) {
    cout << "argc must more than 2\n";
    return 1;
  }
  AVFormatContext *fmt_ctx = nullptr;
  if (avformat_open_input(&fmt_ctx, argv[1], nullptr, nullptr) < 0) {
    cerr << "avformat_open_input error\n";
    return 1;
  }
  if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
    cerr << "avformat_find_stream_info error\n";
    avformat_close_input(&fmt_ctx);
    return 1;
  }
  /*----------------------------SDL初始化部分----------------------------------*/
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
    cerr << "SDL_Init error \n";
    return 1;
  }
  /*-----------------视频-------------------*/
  int video_stream_index = -1;
  video_stream_index =
      av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (video_stream_index == -1) {
    cerr << "No video_stream\n";
    avformat_close_input(&fmt_ctx);
    return 1;
  }
  const AVCodec *video_codec = avcodec_find_decoder(
      fmt_ctx->streams[video_stream_index]->codecpar->codec_id);
  AVCodecContext *video_codec_ctx = avcodec_alloc_context3(video_codec);
  avcodec_parameters_to_context(video_codec_ctx,
                                fmt_ctx->streams[video_stream_index]->codecpar);
  if (avcodec_open2(video_codec_ctx, video_codec, nullptr) < 0) {
    cerr << "avcodec_open2 error\n";
    avformat_close_input(&fmt_ctx);
    return 1;
  }
  /*-------------------音频--------------------------*/
  int audio_stream_index = -1;
  audio_stream_index =
      av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  AVCodecContext *audio_codec_ctx = nullptr;
  SDL_AudioDeviceID audio_dev = 0;
  SwrContext *swr_ctx = nullptr;
  AudioBuffer audio_buf;
  SDL_AudioSpec wanted, obtained;
  AVChannelLayout out_ch_layout;
  int64_t total_seconds = fmt_ctx->duration / AV_TIME_BASE;
  std::string total_time = format_time(total_seconds);
  if (audio_stream_index >= 0) {
    auto audio_codecpar = fmt_ctx->streams[audio_stream_index]->codecpar;
    const AVCodec *audio_codec = avcodec_find_decoder(audio_codecpar->codec_id);
    audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    avcodec_parameters_to_context(audio_codec_ctx, audio_codecpar);
    if (avcodec_open2(audio_codec_ctx, audio_codec, nullptr) < 0) {
      cerr << "avcodec_open2 error\n";
      avcodec_free_context(&audio_codec_ctx);
      avformat_close_input(&fmt_ctx);
      return 1;
    }
    wanted.freq = audio_codec_ctx->sample_rate;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = audio_codec_ctx->ch_layout.nb_channels;
    wanted.samples = 1024;
    wanted.callback = audio_callback;
    wanted.userdata = &audio_buf;
    audio_dev = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
    if (audio_dev == 0) {
      std::cerr << SDL_GetError() << "\n";
    }
    av_channel_layout_default(&out_ch_layout, obtained.channels);
    swr_alloc_set_opts2(&swr_ctx, &out_ch_layout, AV_SAMPLE_FMT_S16,
                        obtained.freq, &audio_codec_ctx->ch_layout,
                        audio_codec_ctx->sample_fmt,
                        audio_codec_ctx->sample_rate, 0, nullptr);
    swr_init(swr_ctx);
  }
  int width = video_codec_ctx->width;
  int height = video_codec_ctx->height;

  SDL_Window *window =
      SDL_CreateWindow("MyPlay", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                       width, height + 15, 0);
  if (!window) {
    cerr << "window is null\n";
    return 1;
  }
  /*---------------------------取帧渲染部分------------------------------------*/
  FrameQueue fq(10);

  std::thread t(decode_thread, fmt_ctx, swr_ctx, video_codec_ctx,
                audio_codec_ctx, video_stream_index, audio_stream_index,
                std::ref(fq), std::ref(audio_buf), std::ref(obtained));
  std::string current_time{};
  std::string title{};
  double current_seconds = 0;
  bool running = true;
  bool paused = false;
  bool unpauseable = true;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dist(0, 255);
  SDL_Renderer *render =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  SDL_Event event;
  SDL_Texture *texture = SDL_CreateTexture(
      render, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, width, height);
  while (running) {
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        running = false;
        break;
      case SDL_KEYDOWN: {
        if (event.key.keysym.sym == SDLK_ESCAPE) {
          running = false;
        }
        if (event.key.keysym.sym == SDLK_SPACE) {
          paused ^= true;
        }
        if (event.key.keysym.sym == SDLK_LEFT) {
          seek = true;
          target_seconds =
              ((current_seconds - 1) > 0 ? current_seconds - 1 : 0);
        }
        if (event.key.keysym.sym == SDLK_RIGHT) {
          seek = true;
          target_seconds = current_seconds + 1;
        }
        break;
      }
      case SDL_MOUSEBUTTONDOWN: {
        std::string btn_name = "";
        if (event.button.button == SDL_BUTTON_LEFT) {
          btn_name = "鼠标左键";
        }
        if (event.button.button == SDL_BUTTON_RIGHT) {
          btn_name = "鼠标右键";
        }
        cout << event.button.x << " " << event.button.y << " " << btn_name
             << "\n";
        if (event.button.y > height) {
          double ratio = event.button.x * 1.0 / width;
          target_seconds = ratio * total_seconds;
          seek = true;
        }
        break;
      }
      default:
        break;
      }
    }
    if (audio_stream_index >= 0) {
      if (paused) {
        if (!unpauseable) {
          SDL_PauseAudioDevice(audio_dev, 1);
          unpauseable = true;
        }
        continue;
      }
      if (unpauseable) {
        SDL_PauseAudioDevice(audio_dev, 0);
        unpauseable = false;
      }
    } else {
      if (paused) {
        continue;
      }
    }
    double diff = 0;
    auto frame = fq.pop();
    if (frame) {
      current_seconds =
          frame->pts * av_q2d(fmt_ctx->streams[video_stream_index]->time_base);
      if (current_seconds < target_seconds - 0.1) {
        av_frame_free(&frame);
        continue;
      }
      diff =
          frame->pts * av_q2d(fmt_ctx->streams[video_stream_index]->time_base) -
          static_cast<double>(audio_buf.total_samples_read()) / obtained.freq;
      if (diff <= -0.05) {
        av_frame_free(&frame);
        continue;
      }
      if (diff >= 0.005) {
        SDL_Delay(static_cast<Uint32>(diff * 1000 * 0.8));
      }
      current_time = format_time(current_seconds);
      title = "MyPlay - " + current_time + " / " + total_time;
      SDL_SetWindowTitle(window, title.data());
      SDL_UpdateYUVTexture(texture, nullptr, frame->data[0], frame->linesize[0],
                           frame->data[1], frame->linesize[1], frame->data[2],
                           frame->linesize[2]);
      SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
      SDL_RenderClear(render);
      SDL_SetRenderDrawColor(render, 80, 80, 80, 255);
      SDL_Rect back_rect{0, height, width, 15};
      SDL_RenderFillRect(render, &back_rect);
      SDL_SetRenderDrawColor(render, 255, 255, 255, 255);
      SDL_Rect front_rect{
          0, height,
          static_cast<int>((current_seconds / total_seconds) * width), 15};
      SDL_RenderFillRect(render, &front_rect);
      SDL_Rect video_rect{0, 0, width, height};
      SDL_RenderCopy(render, texture, nullptr, &video_rect);
      SDL_RenderPresent(render);
      av_frame_free(&frame);
    } else {
      SDL_Delay(10);
      continue;
    }
  }
  cout << "结束\n";
  fq.stop();
  if (t.joinable()) {
    t.join();
  }
  SDL_DestroyRenderer(render);
  SDL_DestroyWindow(window);
  if (audio_stream_index >= 0) {
    swr_free(&swr_ctx);
    av_channel_layout_uninit(&out_ch_layout);
    SDL_CloseAudioDevice(audio_dev);
    avcodec_free_context(&audio_codec_ctx);
  }
  SDL_Quit();
  avcodec_free_context(&video_codec_ctx);
  avformat_close_input(&fmt_ctx);
  return 0;
}
