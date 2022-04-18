//
// Created by blackox626 on 2022/4/18.
//

#ifndef FFMPEGPRO_VIDEODECODER_H
#define FFMPEGPRO_VIDEODECODER_H


#include <string>

class VideoDecoder {

public:
    VideoDecoder();

    ~VideoDecoder();

    void decode_video(std::string media_path, std::string yuv_path);
};


#endif //FFMPEGPRO_VIDEODECODER_H
