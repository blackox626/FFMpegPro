//
// Created by blackox626 on 2022/4/18.
//

#include <iostream>
#include "VideoDecoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

VideoDecoder::VideoDecoder() {}

VideoDecoder::~VideoDecoder() {

}

void VideoDecoder::decode_video(std::string media_path, std::string yuv_path) {
    AVFormatContext *avFormatContext = nullptr;
    AVCodecContext *avCodecContext = nullptr;

    avFormatContext = avformat_alloc_context();
    avformat_open_input(&avFormatContext, media_path.c_str(), nullptr, nullptr);

    av_dump_format(avFormatContext, 0, media_path.c_str(), 0);

    int video_index = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_index < 0) {
        std::cout << "没有找到视频" << std::endl;
    }

    const AVCodec *avCodec = avcodec_find_decoder(avFormatContext->streams[video_index]->codecpar->codec_id);
    avCodecContext = avcodec_alloc_context3(avCodec);
    avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[video_index]->codecpar);
    int ret = avcodec_open2(avCodecContext, avCodec, nullptr);
    if (ret < 0) {
        std::cout << "解码器打开失败" << std::endl;
    }

    FILE *yuv_file = fopen(yuv_path.c_str(), "wb");

    AVPacket *avPacket = av_packet_alloc();
    AVFrame *avFrame = av_frame_alloc();
    while (true) {
        ret = av_read_frame(avFormatContext, avPacket);
        if (ret < 0) {
            std::cout << "文件读取完毕" << std::endl;
            break;
        } else if (video_index == avPacket->stream_index) {
            ret = avcodec_send_packet(avCodecContext, avPacket);
            if (ret < 0) {
                std::cout << "视频发送解码失败:" << av_err2str(ret) << std::endl;
            }
            while (true) {
                ret = avcodec_receive_frame(avCodecContext, avFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    std::cout << "avcodec_receive_frame：" << av_err2str(ret) << std::endl;
                    break;
                } else if (ret < 0) {
                    std::cout << "视频解码失败：" << std::endl;
                    return;
                } else {
                    std::cout << "写入YUV文件avFrame->linesize[0]：" << avFrame->linesize[0] << "avFrame->width:"
                              << avFrame->width << std::endl;

                    std::cout << "写入YUV文件avFrame->linesize[1]：" << avFrame->linesize[1] << "avFrame->width:"
                              << avFrame->width << std::endl;

                    std::cout << "写入YUV文件avFrame->linesize[2]：" << avFrame->linesize[2] << "avFrame->width:"
                              << avFrame->width << std::endl;

                    std::cout << "avFrame->format：" << avFrame->format << std::endl;
                    // 播放 ffplay -i YUV文件路径 -pixel_format yuv420p -framerate 25 -video_size 640x480
                    // frame->linesize[1]  对齐的问题
                    // 正确写法  linesize[]代表每行的字节数量，所以每行的偏移是linesize[]
                    // 成员data是个指针数组，每个成员所指向的就是yuv三个分量的实体数据了，成员linesize是指对应于每一行的大小，为什么需要这个变量，是因为在YUV格式和RGB格式时，每行的大小不一定等于图像的宽度
                    //

                    //ptr -- 这是指向要被写入的元素数组的指针。
                    //size -- 这是要被写入的每个元素的大小，以字节为单位。
                    //nmemb -- 这是元素的个数，每个元素的大小为 size 字节。
                    //stream -- 这是指向 FILE 对象的指针，该 FILE 对象指定了一个输出流。
                    for (int j = 0; j < avFrame->height; j++)
                        fwrite(avFrame->data[0] + j * avFrame->linesize[0], 1, avFrame->width, yuv_file);
                    for (int j = 0; j < avFrame->height / 2; j++)
                        fwrite(avFrame->data[1] + j * avFrame->linesize[1], 1, avFrame->width / 2, yuv_file);
                    for (int j = 0; j < avFrame->height / 2; j++)
                        fwrite(avFrame->data[2] + j * avFrame->linesize[2], 1, avFrame->width / 2, yuv_file);

                    // 错误写法 用source.200kbps.766x322_10s.h264测试时可以看出该种方法是错误的
                    // 如果frame.width == avFrame->linesize[0] 则可以用这种方式写入
                    //  写入y分量
//        fwrite(avFrame->data[0], 1, avFrame->width * avFrame->height,  yuv_file);//Y
//        // 写入u分量
//        fwrite(avFrame->data[1], 1, (avFrame->width) *(avFrame->height)/4,yuv_file);//U:宽高均是Y的一半
//        //  写入v分量
//        fwrite(avFrame->data[2], 1, (avFrame->width) *(avFrame->height)/4,yuv_file);//V：宽高均是Y的一半
                }
            }
        }
        av_packet_unref(avPacket);
    }

    fflush(yuv_file);
    av_packet_free(&avPacket);
    av_frame_free(&avFrame);

    if (nullptr != yuv_file) {
        fclose(yuv_file);
        yuv_file = nullptr;
    }
}
