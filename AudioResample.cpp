//
// Created by blackox626 on 2022/4/24.
//

#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

#ifndef AUDIO_TARGET_SAMPLE
#define AUDIO_TARGET_SAMPLE 48000
#endif

class AudioResample {

public:
    // 将PCM数据重采样
    void decode_audio_resample(const char *media_path, const char *pcm_path) {
        avFormatContext = avformat_alloc_context();
        int ret = avformat_open_input(&avFormatContext, media_path, nullptr, nullptr);
        if (ret < 0) {
            std::cout << "输入打开失败" << std::endl;
            return;
        }
        // 寻找视频流
        int audio_index = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (audio_index < 0) {
            std::cout << "没有可用的音频流" << std::endl;
            return;
        }
        // 配置解码相关
        const AVCodec *avCodec = avcodec_find_decoder(avFormatContext->streams[audio_index]->codecpar->codec_id);
        avCodecContext = avcodec_alloc_context3(avCodec);
        avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[audio_index]->codecpar);
        ret = avcodec_open2(avCodecContext, avCodec, nullptr);
        if (ret < 0) {
            std::cout << "解码器打开失败" << std::endl;
            return;
        }
        // 分配包和帧数据结构
        avPacket = av_packet_alloc();
        avFrame = av_frame_alloc();

        // 打开yuv输出文件
        pcm_out = fopen(pcm_path, "wb");
        // 读取数据解码
        while (true) {
            ret = av_read_frame(avFormatContext, avPacket);
            if (ret < 0) {
                std::cout << "音频包读取完毕" << std::endl;
                break;
            } else {
                if (avPacket->stream_index == audio_index) {
                    // 只处理音频包
                    ret = avcodec_send_packet(avCodecContext, avPacket);
                    if (ret < 0) {
                        std::cout << "发送解码包失败" << std::endl;
                        return;
                    }
                    while (true) {
                        ret = avcodec_receive_frame(avCodecContext, avFrame);
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                            break;
                        } else if (ret < 0) {
                            std::cout << "获取解码数据失败" << std::endl;
                            return;
                        } else {
                            std::cout << "重采样解码数据" << std::endl;
                            resample();
                        }
                    }
                }
            }
            av_packet_unref(avPacket);
        }
    }

    ~AudioResample() {
        // todo 释放资源
    }

private:

    AVFormatContext *avFormatContext = nullptr;
    AVCodecContext *avCodecContext = nullptr;
    AVPacket *avPacket = nullptr;
    AVFrame *avFrame = nullptr;
    FILE *pcm_out = nullptr;
    SwrContext *swrContext = nullptr;
    AVFrame *out_frame = nullptr;
    int64_t max_dst_nb_samples;

    /**
     * 重采样并输出到文件
     */
    void resample() {
        if (nullptr == swrContext) {
            /**
             * 以下可以使用 swr_alloc、av_opt_set_channel_layout、av_opt_set_int、av_opt_set_sample_fmt
             * 等API设置，更加灵活
             */
            swrContext = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, AUDIO_TARGET_SAMPLE,
                                            avFrame->channel_layout, static_cast<AVSampleFormat>(avFrame->format),
                                            avFrame->sample_rate, 0, nullptr);
            swr_init(swrContext);
        }
        // 进行音频重采样
        int src_nb_sample = avFrame->nb_samples;
        // 为了保持从采样后 dst_nb_samples / dest_sample = src_nb_sample / src_sample_rate
        max_dst_nb_samples = av_rescale_rnd(src_nb_sample, AUDIO_TARGET_SAMPLE, avFrame->sample_rate, AV_ROUND_UP);
        // 从采样器中会缓存一部分，获取缓存的长度
        int64_t delay = swr_get_delay(swrContext, avFrame->sample_rate);
        int64_t dst_nb_samples = av_rescale_rnd(delay + avFrame->nb_samples, AUDIO_TARGET_SAMPLE, avFrame->sample_rate,
                                                AV_ROUND_UP);
        if (nullptr == out_frame) {
            init_out_frame(dst_nb_samples);
        }

        if (dst_nb_samples > max_dst_nb_samples) {
            // 需要重新分配buffer
            std::cout << "需要重新分配buffer" << std::endl;
            init_out_frame(dst_nb_samples);
            max_dst_nb_samples = dst_nb_samples;
        }
        // 重采样
        int ret = swr_convert(swrContext, out_frame->data, dst_nb_samples,
                              const_cast<const uint8_t **>(avFrame->data), avFrame->nb_samples);

        if (ret < 0) {
            std::cout << "重采样失败" << std::endl;
        } else {
            // 每帧音频数据量的大小
            int data_size = av_get_bytes_per_sample(static_cast<AVSampleFormat>(out_frame->format));

            std::cout << "重采样成功：" << ret << "----dst_nb_samples:" << dst_nb_samples << "---data_size:" << data_size
                      << std::endl;
            // 交错模式保持写入
            // 注意不要用 i < out_frame->nb_samples， 因为重采样出来的点数不一定就是out_frame->nb_samples
            for (int i = 0; i < ret; i++) {
                for (int ch = 0; ch < out_frame->channels; ch++) {
                    // 需要储存为pack模式
                    fwrite(out_frame->data[ch] + data_size * i, 1, data_size, pcm_out);
                }
            }
        }
    }

    void init_out_frame(int64_t dst_nb_samples) {
        av_frame_free(&out_frame);
        out_frame = av_frame_alloc();
        out_frame->sample_rate = AUDIO_TARGET_SAMPLE;
        out_frame->format = AV_SAMPLE_FMT_FLTP;
        out_frame->channel_layout = AV_CH_LAYOUT_STEREO;
        out_frame->nb_samples = dst_nb_samples;
        // 分配buffer
        av_frame_get_buffer(out_frame, 0);
        av_frame_make_writable(out_frame);
    }

};