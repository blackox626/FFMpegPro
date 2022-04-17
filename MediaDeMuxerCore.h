//
// Created by blackox626 on 2022/4/16.
//

#ifndef FFMPEGPRO_MEDIADEMUXERCORE_H
#define FFMPEGPRO_MEDIADEMUXERCORE_H

#include <iostream>

class MediaDeMuxerCore {

public:
    MediaDeMuxerCore();

    ~MediaDeMuxerCore();

    // 提取视频 h264裸流
    void de_muxer_video(std::string media_path,std::string out_video_path);
    // 提取音频 例如aac流
    void de_muxer_audio(std::string media_path,std::string out_audio_path);
    // 使用容器封装的方式提取aac流
    void de_muxer_audio_by_stream(std::string media_path,std::string out_audio_path);
};


#endif //FFMPEGPRO_MEDIADEMUXERCORE_H
