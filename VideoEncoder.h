//
// Created by blackox626 on 2022/4/21.
//

#ifndef FFMPEGPRO_VIDEOENCODER_H
#define FFMPEGPRO_VIDEOENCODER_H


class VideoEncoder {
public:
    virtual ~VideoEncoder();

    VideoEncoder();

    void encode_yuv_to_h264(const char *yuv_path, const char *h264_path);
};


#endif //FFMPEGPRO_VIDEOENCODER_H
