#include <iostream>
#include "MediaDeMuxerCore.h"

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

    MediaDeMuxerCore *deMuxerCore = new MediaDeMuxerCore();
    std::string media_path = "/Users/blackox626/CLionProjects/FFMpegPro/resource/media.mp4";
    std::string out_video_path = "/Users/blackox626/CLionProjects/FFMpegPro/resource/video.h264";
    deMuxerCore->de_muxer_video(media_path,out_video_path);

    std::string out_audio_path = "/Users/blackox626/CLionProjects/FFMpegPro/resource/audio.acc";
    deMuxerCore->de_muxer_audio(media_path,out_audio_path);

    return 0;
}
