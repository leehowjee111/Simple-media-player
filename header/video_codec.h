#pragma once
#include "codec.h"

class VideoCodec : public Codec {
public:
  ~VideoCodec();
  bool init(AVCodecParameters *) override;
  int send_packet(AVPacket *) override;
  int decode(AVFrame *) override;
  void flush() override;
  void clean() override;

private:
};
