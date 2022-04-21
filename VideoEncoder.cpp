//
// Created by blackox626 on 2022/4/21.
//

#include <iostream>
#include "VideoEncoder.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

VideoEncoder::~VideoEncoder() {

}

VideoEncoder::VideoEncoder() {}

static FILE *h264_out = nullptr;

void encode_video(AVCodecContext *avCodecContext, AVFrame *avFrame, AVPacket *avPacket) {
    int ret = avcodec_send_frame(avCodecContext, avFrame);
    if (ret < 0) {
        std::cout << "yuv发送编码失败" << std::endl;
    }
    while (true) {
        ret = avcodec_receive_packet(avCodecContext, avPacket);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            std::cout << "需要输送更多yuv数据" << std::endl;
            break;
        }

        std::cout << "写入文件h264" << std::endl;
        fwrite(avPacket->data, 1, avPacket->size, h264_out);
        av_packet_unref(avPacket);
    }
}

void VideoEncoder::encode_yuv_to_h264(const char *yuv_path, const char *h264_path) {
    const AVCodec *avCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    AVCodecContext *avCodecContext = avcodec_alloc_context3(avCodec);
    avCodecContext->time_base = {1, 25};
    // 配置编码器参数
    avCodecContext->width = 720;
    avCodecContext->height = 1280;
    avCodecContext->bit_rate = 2000000;
    avCodecContext->profile = FF_PROFILE_H264_MAIN;
    avCodecContext->gop_size = 10;
    avCodecContext->time_base = {1, 25};
    avCodecContext->framerate = {25, 1};
    // b帧的数量
    avCodecContext->max_b_frames = 1;
    avCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    // 设置H264的编码器参数为延迟模式，提高编码质量，但是会造成编码速度下降
    av_opt_set(avCodecContext->priv_data, "preset", "slow", 0);
    // 打开编码器
    int ret = avcodec_open2(avCodecContext, avCodec, nullptr);
    if (ret < 0) {
        std::cout << "编码器打开失败:" << strerror(ret) << std::endl;
        // todo 在析构函数中释放资源
        return;
    }

    AVPacket *avPacket = av_packet_alloc();
    AVFrame *avFrame = av_frame_alloc();
    avFrame->width = avCodecContext->width;
    avFrame->height = avCodecContext->height;
    avFrame->format = avCodecContext->pix_fmt;
    // 为frame分配buffer
    av_frame_get_buffer(avFrame, 0);
    av_frame_make_writable(avFrame);

    h264_out = fopen(h264_path, "wb");
    // 读取yuv数据送入编码器
    FILE *input_media = fopen(yuv_path, "r");
    if (nullptr == input_media) {
        std::cout << "输入文件打开失败" << std::endl;
        return;
    }

    int pts = 0;
    while (!feof(input_media)) {
        int64_t frame_size = avFrame->width * avFrame->height * 3 / 2;
        int64_t read_size = 0;
        // 这里可以自行了解下ffmpeg字节对齐的问题
        // 内存对齐 提高效率
        if (avFrame->width == avFrame->linesize[0]) {
            std::cout << "不存在padding字节" << std::endl;
            // 读取y
            read_size += fread(avFrame->data[0], 1, avFrame->width * avFrame->height, input_media);
            // 读取u
            read_size += fread(avFrame->data[1], 1, avFrame->width * avFrame->height / 4, input_media);
            // 读取v
            read_size += fread(avFrame->data[2], 1, avFrame->width * avFrame->height / 4, input_media);
        } else {
            std::cout << "存在padding字节" << std::endl;
            // 需要对YUV分量进行逐行读取
            for (int i = 0; i < avFrame->height; i++) {
                // 读取y
                read_size += fread(avFrame->data[0] + i * avFrame->linesize[0], 1, avFrame->width, input_media);
            }
            // 读取u和v
            for (int i = 0; i < avFrame->height / 2; i++) {
                read_size += fread(avFrame->data[1] + i * avFrame->linesize[1], 1, avFrame->width / 2, input_media);
                read_size += fread(avFrame->data[2] + i * avFrame->linesize[2], 1, avFrame->width / 2, input_media);
            }
        }
        pts += (1000000 / 25);
        avFrame->pts = pts;
        if (read_size != frame_size) {
            std::cout << "读取数据有误:" << std::endl;
        }
        encode_video(avCodecContext, avFrame, avPacket);
    }

    // 冲刷编码器
    encode_video(avCodecContext, nullptr, avPacket);
    fflush(h264_out);
}