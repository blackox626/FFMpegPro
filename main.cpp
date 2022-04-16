#include <iostream>

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
    std::cout << "av_version_info:" << av_version_info() << std::endl;
    std::cout << "av_version_info:" << avcodec_configuration() << std::endl;
    return 0;
}
