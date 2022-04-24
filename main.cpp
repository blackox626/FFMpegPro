#include <iostream>
#include "MediaDeMuxerCore.h"
#include "AudioDecoder.h"
#include "VideoDecoder.h"

#include "AudioResample.cpp"

//在C++中引用C语言中的函数和变量，在包含C语言头文件（假设为cExample.h）时，需进行下列处理：
//extern "C"
//{
//#include "cExample.h"
//}

extern "C" {
#include "libavcodec/avcodec.h"
#include <libavformat/avformat.h>
#include "libavutil/avutil.h"
}

int main() {
//    std::cout << "Hello, World!" << std::endl;

    // 打印ffmpeg的信息
//    std::cout << "av_version_info:" << av_version_info() << std::endl;
//    std::cout << "av_version_info:" << avcodec_configuration() << std::endl;


    std::string media_path = "/Users/blackox626/CLionProjects/FFMpegPro/resource/media.mp4";

//    MediaDeMuxerCore *deMuxerCore = new MediaDeMuxerCore();

//    std::string out_video_path = "/Users/blackox626/CLionProjects/FFMpegPro/resource/video.h264";
//    deMuxerCore->de_muxer_video(media_path,out_video_path);
//
//    std::string out_audio_path = "/Users/blackox626/CLionProjects/FFMpegPro/resource/audio.aac";
//    deMuxerCore->de_muxer_audio(media_path,out_audio_path);

//    VideoDecoder *videoDecoder = new VideoDecoder();
//    std::string yuv_path = "/Users/blackox626/CLionProjects/FFMpegPro/resource/video.yuv";
//    videoDecoder->decode_video(media_path, yuv_path);

//    AudioDecoder *audioDecoder = new AudioDecoder();
//    std::string pcm_path = "/Users/blackox626/CLionProjects/FFMpegPro/resource/audio.pcm";
//    audioDecoder->decode_audio(media_path,pcm_path);

    /// 局部变量 栈上分配空间 函数结束 自动析构 释放内存
    AudioResample audioResample;
    const char *_media_path = "/Users/blackox626/CLionProjects/FFMpegPro/resource/media.mp4";
    const char *resample_pcm_path = "/Users/blackox626/CLionProjects/FFMpegPro/resource/audio_resample.pcm";
    audioResample.decode_audio_resample(_media_path,resample_pcm_path);

    return 0;
}
